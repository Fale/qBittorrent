/*
 *   Copyright (C) 2007 by Ishan Arora
 *   ishanarora@gmail.com
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


#include "httpconnection.h"
#include "httpserver.h"
#include "eventmanager.h"
#include "json.h"
#include <QTcpSocket>
#include <QDateTime>
#include <QStringList>
#include <QHttpRequestHeader>
#include <QHttpResponseHeader>
#include <QFile>
#include <QDebug>

HttpConnection::HttpConnection(QTcpSocket *socket, HttpServer *parent)
	: QObject(parent), socket(socket), parent(parent)
{
	socket->setParent(this);
	connect(socket, SIGNAL(readyRead()), this, SLOT(read()));
	connect(socket, SIGNAL(disconnected()), this, SLOT(deleteLater()));
}

HttpConnection::~HttpConnection()
{
}

void HttpConnection::read()
{
	QString input = socket->readAll();
	qDebug(" -------");
	qDebug("|REQUEST|");
	qDebug(" -------");
	qDebug(input.toAscii().constData());
	parser.write(input);
	if(parser.isError())
	{
		generator.setStatusLine(400, "Bad Request");
		write();
	}
	else
		if (parser.isParsable())
			respond();
}

void HttpConnection::write()
{
	QByteArray output = generator.toByteArray();
	qDebug(" --------");
	qDebug("|RESPONSE|");
	qDebug(" --------");
	qDebug()<<output;
	socket->write(output);
	socket->disconnectFromHost();
}

void HttpConnection::respond()
{
	QStringList auth = parser.value("Authorization").split(" ", QString::SkipEmptyParts);
	if (auth.size() != 2 || QString::compare(auth[0], "Basic", Qt::CaseInsensitive) != 0 || !parent->isAuthorized(auth[1].toUtf8()))
	{
		generator.setStatusLine(401, "Unauthorized");
		generator.setValue("WWW-Authenticate",  "Basic realm=\"you know what\"");
		write();
		return;
	}
	QString url  = parser.url();
	QStringList list = url.split('/', QString::SkipEmptyParts);
	if (list.contains(".") || list.contains(".."))
	{
		respondNotFound();
		return;
	}
	if (list.size() == 0)
		list.append("index.html");
	if (list.size() == 2)
	{
		if (list[0] == "json")
		{
			if (list[1] == "events")
			{
				EventManager* manager =  parent->eventManager();
				uint r = parser.get("r").toUInt();
				if(manager->isUpdated(r))
					respondJson();
				else
					connect(manager, SIGNAL(updated()), this, SLOT(respondJson()));
				return;
			}
		}
		if (list[0] == "command")
		{
			QString command = list[1];
			respondCommand(command);
			generator.setStatusLine(200, "OK");
			write();
			return;
		}
	}
	if (list[0] == "images")
		list[0] = "Icons";
	else
		list.prepend("webui");
	url = ":/" + list.join("/");
	QFile file(url);
	if(!file.open(QIODevice::ReadOnly))
	{
		respondNotFound();
		return;
	}
	QString ext = list.last();
	int index = ext.lastIndexOf('.') + 1;
	if (index > 0)
		ext.remove(0, index);
	else
		ext.clear();
	QByteArray data = file.readAll();
	generator.setStatusLine(200, "OK");
	generator.setContentTypeByExt(ext);
	generator.setMessage(data);
	write();
}

void HttpConnection::respondNotFound()
{
	generator.setStatusLine(404, "File not found");
	write();
}

void HttpConnection::respondJson()
{
	EventManager* manager =  parent->eventManager();
	QString temp = parser.get("r");
	uint r = parser.get("r").toUInt();
	QVariant data = manager->querySince(r);
	QString string = toJson(data);
	generator.setStatusLine(200, "OK");
	generator.setContentTypeByExt("js");
	generator.setMessage(string);
	write();
}

void HttpConnection::respondCommand(QString command)
{
	if(command == "download")
	{
		QString urls = parser.post("urls");
		QStringList list = urls.split('\n');
		QStringList url_list_cleaned;
		foreach(QString url, list){
			url = url.trimmed();
			if(!url.isEmpty()){
				if(url_list_cleaned.indexOf(QRegExp(url, Qt::CaseInsensitive, QRegExp::FixedString)) < 0){
					url_list_cleaned << url;
				}
			}
		}
		emit urlsReadyToBeDownloaded(url_list_cleaned);
		return;
	}
	if(command == "resumeall")
	{
		emit resumeAllTorrents();
		return;
	}
	if(command == "pauseall")
	{
		emit pauseAllTorrents();
		return;
	}
	if(command == "resume")
	{
		emit resumeTorrent(parser.post("hash"));
		return;
	}
	if(command == "pause")
	{
		emit pauseTorrent(parser.post("hash"));
		return;
	}
	if(command == "delete")
	{
		emit deleteTorrent(parser.post("hash"));
		return;
	}
}
