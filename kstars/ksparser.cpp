/***************************************************************************
                          KSParser.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : 2012/24/06
    copyright            : (C) 2012 by Rishab Arora
    email                : ra.rishab@gmail.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "ksparser.h"
#include <kdebug.h>
#include <klocale.h>


//TODO: 
KSParser::KSParser(QString filename, char skipChar, char delimiter, QList<DataTypes> pattern, QList<QString> names)
		  :m_Filename(filename),pattern(pattern),names(names),skipChar(skipChar){
    if (!m_FileReader.open(m_Filename) || pattern.length() != names.length()) {
        kWarning() <<"Unable to open file!";
        readFunctionPtr = &KSParser::DummyCSVRow;
    } else  readFunctionPtr = &KSParser::ReadCSVRow;

}

KSParser::KSParser(QString filename, char skipChar, QList<int> widths) {
    
        readFunctionPtr = &KSParser::ReadFixedWidthRow;

}

QHash<QString,QVariant>  KSParser::ReadNextRow() {
    return (this->*readFunctionPtr)();
}

#define EBROKEN_DOUBLE 0.0
#define EBROKEN_FLOATS 0.0
#define EBROKEN_INT 0

QHash<QString,QVariant>  KSParser::ReadCSVRow() {
   
    //This signifies that someone tried to read a row
    //without checking if hasNextRow is true
    if (m_FileReader.hasMoreLines() == false)
        return DummyCSVRow(); 
    
    QString line;
    QStringList separated;
    bool success = false;
    QHash<QString,QVariant> newRow;
    
    while (m_FileReader.hasMoreLines() && success == false) {
        line = m_FileReader.readLine();
	if (line[0] == skipChar) continue;
        separated = line.split(",");
	/*
	 * TODO: here's how to go about the parsing
	 * 1) split along ,
	 * 2) check first and last characters. 
	 *    if the first letter is  '"',
	 *    then combine the nexto ones in it till
	 *    till you come across the next word which
	 *    has the last character as '"'
	 *
	*/
	/*
	for (QStringListIterator i; i!= separated.end(); ++i){
	    if (*i[0] == '#') {
		
	    }
	}
	
	*/
        if (separated.length() != pattern.length())
            continue;
        for (int i=0; i<pattern.length(); i++) {
	    bool ok;
            switch (pattern[i]){
                case D_QSTRING:
                    newRow[names[i]]=separated[i];
                    break;
                case D_DOUBLE:
                    newRow[names[i]]=separated[i].toDouble(&ok);
		    if (!ok) {
		      kWarning() <<  "toDouble Failed at line : " << line;
		      newRow[names[i]] = EBROKEN_DOUBLE;
		    }
                    break;
                case D_INT:
                    newRow[names[i]]=separated[i].toInt(&ok);
		    if (!ok) {
		      if (!ok) kWarning() << "toInt Failed at line : "<< line;
		      newRow[names[i]] = EBROKEN_INT;
		    }
                    break;
                case D_FLOAT:
                    newRow[names[i]]=separated[i].toFloat(&ok);
		    if (!ok) {
		      if (!ok) kWarning() << "toFloat Failed at line : "<< line;
		      newRow[names[i]] = EBROKEN_FLOATS;
		    }
                    break;
            }
        }
        success = true;
        
    }
    
//     if (m_FileReader.hasMoreLines() == false)
//         m_FileReader.close??
    
    return newRow;
}

QHash<QString,QVariant>  KSParser::ReadFixedWidthRow() {
    kWarning() <<"READ FWR";
    QHash<QString,QVariant> newRow;
    return newRow;
}

QHash<QString,QVariant>  KSParser::DummyCSVRow() {
    //TODO: allot 0 or "null" to every position
    QHash<QString,QVariant> newRow;
    for (int i=0; i<pattern.length(); i++){
            switch (pattern[i]){
                case D_QSTRING:
                    newRow[names[i]]="Null";
                    break;
                case D_DOUBLE:
                    newRow[names[i]]=0.0;
                    break;
                case D_INT:
                    newRow[names[i]]=0;
                    break;
                case D_FLOAT:
                    newRow[names[i]]=0.0;
                    break;
            }
        }
    kWarning() <<"READ FWR";
    return newRow;
}

bool KSParser::hasNextRow() {
    return m_FileReader.hasMoreLines();
    
}

