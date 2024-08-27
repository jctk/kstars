/*
    SPDX-FileCopyrightText: 2012 Jasem Mutlaq <mutlaqja@ikarustech.com>
    SPDX-FileCopyrightText: 2024 Wolfgang Reissenberger <sterne-jaeger@openfuture.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "capturemodulestate.h"
#include "camera.h"

#include "ekos_capture_debug.h"

namespace Ekos
{

CaptureModuleState::CaptureModuleState(QObject *parent): QObject{parent} {}

void CaptureModuleState::addCamera(QSharedPointer<Camera> newCamera)
{
    mutableCameras().append(newCamera);
    connect(newCamera.get(), &Camera::newStatus, this, &CaptureModuleState::captureStateChanged);
    connect(newCamera.get(), &Camera::requestAction, this, &CaptureModuleState::handleActionRequest);

    // prepare dithering enforcing timeout
    m_DitheringTimer.setSingleShot(true);
    connect(&m_DitheringTimer, &QTimer::timeout, this, &CaptureModuleState::startDithering);
    // check what to do next
    checkActiveActions();
}

void CaptureModuleState::removeCamera(int pos)
{
    if (pos > m_Cameras.count() - 1)
        return;

    auto cam = m_Cameras.at(pos);
    // clear connections
    cam->disconnect(this);

    // check what to do next
    checkActiveActions();
}

void CaptureModuleState::captureStateChanged(CaptureState newstate, const QString &trainname, int cameraID)
{
    Q_UNUSED(trainname)

    switch (newstate)
    {
        case CAPTURE_IDLE:
        case CAPTURE_PAUSED:
        case CAPTURE_IMAGE_RECEIVED:
        case CAPTURE_COMPLETE:
        case CAPTURE_SUSPENDED:
        case CAPTURE_WAITING:
            if (activeAction(cameraID) == CAPTURE_ACTION_PAUSE)
                // capturing of a single frame completed, clear the active action
                setActiveAction(cameraID, CAPTURE_ACTION_NONE);
            break;
        case CAPTURE_ABORTED:
            clearAllActions(cameraID);
            break;
        default:
            // do nothing
            break;
    }
    // check what to do next
    checkActiveActions();
}

void CaptureModuleState::setGuideStatus(GuideState newstate)
{
    switch (newstate)
    {
        case GUIDE_DITHERING_SUCCESS:
        case GUIDE_DITHERING_ERROR:
            foreach (auto cam, cameras())
            {
                // dithering action finished
                if (activeAction(cam->cameraId()) == CAPTURE_ACTION_DITHER)
                    setActiveAction(cam->cameraId(), CAPTURE_ACTION_NONE);
                // restart all cameras that have been stopped for dithering
                if (cam->state()->isCaptureStopped() || cam->state()->isCapturePausing())
                    enqueueAction(cam->cameraId(), CAPTURE_ACTION_START);
            }
            break;
        default:
            // do nothing
            break;
    }

    // forward state to cameras
    foreach (auto cam, cameras())
        cam->state()->setGuideState(newstate);

    // check what to do next
    checkActiveActions();
}

CaptureWorkflowActionType CaptureModuleState::activeAction(int cameraID)
{
    if (m_activeActions.contains(cameraID))
        return m_activeActions[cameraID];
    else
        return CAPTURE_ACTION_NONE;
}

void CaptureModuleState::setActiveAction(int cameraID, CaptureWorkflowActionType action)
{
    if (action == CAPTURE_ACTION_NONE)
        m_activeActions.remove(cameraID);
    else
        m_activeActions[cameraID] = action;
}

void CaptureModuleState::handleActionRequest(int cameraID, CaptureWorkflowActionType action)
{
    qCDebug(KSTARS_EKOS_CAPTURE) << "Handling" << action << "request for camera" << cameraID;
    switch(action)
    {
        case CAPTURE_ACTION_DITHER_REQUEST:
            setActiveAction(cameraID, action);
            prepareDitheringAction(cameraID);
            break;
        default:
            // do nothing
            break;
    }
    // check what's up next
    checkActiveActions();
}

void CaptureModuleState::enqueueAction(int cameraID, CaptureWorkflowActionType action)
{
    getActionQueue(cameraID).enqueue(action);
}

void CaptureModuleState::checkActiveActions()
{
    foreach (auto cam, cameras())
    {
        const int cameraID = cam->cameraId();
        switch (activeAction(cameraID))
        {
            case CAPTURE_ACTION_NONE:
                checkNextActionExecution(cameraID);
                break;
            case CAPTURE_ACTION_DITHER_REQUEST:
                checkReadyForDithering(cameraID);
                break;
            default:
                // nothing to do
                break;
        }
    }
}

QQueue<CaptureWorkflowActionType> &CaptureModuleState::getActionQueue(int cameraId)
{
    if (!m_actionQueues.contains(cameraId))
        m_actionQueues[cameraId] = QQueue<CaptureWorkflowActionType>();

    return m_actionQueues[cameraId];
}

void CaptureModuleState::checkNextActionExecution(int cameraID)
{
    if (cameraID == -1)
    {
        foreach (auto cam, cameras())
            checkNextActionExecution(cam->cameraId());

        return;
    }
    QQueue<CaptureWorkflowActionType> &actionQueue = getActionQueue(cameraID);

    // nothing to do, if either there is an active action or the queue is empty
    if (m_activeActions[cameraID] != CAPTURE_ACTION_NONE || actionQueue.isEmpty())
        return;

    auto action = actionQueue.dequeue();
    QSharedPointer<Camera> cam = cameras()[cameraID];
    switch (action)
    {
        case CAPTURE_ACTION_START:
            if (cam->state()->isCaptureStopped())
                cam->start();
            else if (cam->state()->isCapturePausing())
                cam->toggleSequence();
            break;
        case CAPTURE_ACTION_PAUSE:
            setActiveAction(cameraID, CAPTURE_ACTION_PAUSE);
            cam->pause();
            // pause immediately by suspending
            if (!getActionQueue(cameraID).isEmpty() && actionQueue.head() == CAPTURE_ACTION_SUSPEND)
            {
                actionQueue.dequeue();
                cam->suspend();
            }
            break;
        case CAPTURE_ACTION_SUSPEND:
            setActiveAction(cameraID, CAPTURE_ACTION_SUSPEND);
            cam->suspend();
            // check if we should pause immediately

            break;
        default:
            qCWarning(KSTARS_EKOS_CAPTURE) << "No activity defined for action" << action;
            break;
    }
    // check what to do next
    checkActiveActions();
}

void CaptureModuleState::clearAllActions(int cameraID)
{
    setActiveAction(cameraID, CAPTURE_ACTION_NONE);
    getActionQueue(cameraID).clear();
}

void CaptureModuleState::prepareDitheringAction(int cameraId)
{
    const double maxRemainingExposureTime = 0.5 * mutableCameras()[cameraId]->activeJob()->getCoreProperty(
            SequenceJob::SJ_Exposure).toDouble();
    foreach (auto cam, cameras())
    {
        if (cam->state()->isCaptureRunning() && cam->cameraId() != cameraId)
        {
            if (cameraId > 0 || cam->activeJob()->getExposeLeft() < maxRemainingExposureTime)
            {
                // wait until this capture finishes, request pausing (except for lead camera)
                if (cam->cameraId() > 0)
                    enqueueAction(cam->cameraId(), CAPTURE_ACTION_PAUSE);
            }
            else
            {
                // pause to avoid that capturing gets restarted automatically after suspending
                enqueueAction(cam->cameraId(), CAPTURE_ACTION_PAUSE);
                // suspend, it would take to long to finish
                enqueueAction(cam->cameraId(), CAPTURE_ACTION_SUSPEND);
            }
        }
    }
    // check what to do next
    checkActiveActions();
    // start the dithering timer to ensure that we do not wait infinitely
    if (! m_DitheringTimer.isActive())
        m_DitheringTimer.start(maxRemainingExposureTime * 1000);
}

void CaptureModuleState::checkReadyForDithering(int cameraId)
{
    // check if there is still another camera capturing
    bool ready = true;
    foreach (auto cam, cameras())
        if (cam->state()->isCaptureRunning() && cam->cameraId() != cameraId)
        {
            ready = false;
            qCDebug(KSTARS_EKOS_CAPTURE) << "Dithering requested by camera" << cameraId << "blocked by camera" << cam->cameraId();
            break;
        }

    if (ready)
    {
        qCInfo(KSTARS_EKOS_CAPTURE) << "Execute dithering requested by camera" << cameraId;
        setActiveAction(cameraId, CAPTURE_ACTION_DITHER);
        foreach(auto cam, cameras())
        {
            const int id = cam->cameraId();
            // clear dithering requests for all other cameras
            if (id != cameraId && activeAction(id) == CAPTURE_ACTION_DITHER_REQUEST)
                setActiveAction(id, CAPTURE_ACTION_DITHER);
        }
        // stop the timeout
        if (m_DitheringTimer.isActive())
            m_DitheringTimer.stop();

        startDithering();
    }
}

void CaptureModuleState::startDithering()
{
    // find first camera that has requested dithering
    bool found = false;
    foreach (auto id, m_activeActions.keys())
        if (activeAction(id) == CAPTURE_ACTION_DITHER_REQUEST)
        {
            setActiveAction(id, CAPTURE_ACTION_DITHER);
            found = true;
        }
        else if (activeAction(id) == CAPTURE_ACTION_DITHER)
            found = true;

    // do nothing if no request found
    if (found == false)
        return;

    // abort all other running captures that do not requested a dither
    foreach (auto cam, cameras())
        if (cam->state()->isCaptureRunning() && activeAction(cam->cameraId()) != CAPTURE_ACTION_DITHER)
        {
            qCInfo(KSTARS_EKOS_CAPTURE) << "Aborting capture of camera" << cam->cameraId() << "before dithering starts";
            cam->abort();
        }

    // dither
    emit dither();
}

} // namespace Ekos
