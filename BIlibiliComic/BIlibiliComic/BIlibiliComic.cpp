#include "BIlibiliComic.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkAccessManager>

#include <QJsonDocument>
#include <QJsonParseError>
#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <QMap>
#include <QDir>
#include <QMessageBox>

#include <windows.h>
#include "iostream"

#include "zip/zip.h"
#include "zip/unzip.h"

//获取漫画章节详细信息完成事件
void BIlibiliComic::requestDetailFinished(QNetworkReply* reply)
{

	connect(this, SIGNAL(onDetailFinished()),this, SLOT(parsePerEP()));

	// 获取http状态码
	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid())
		qDebug() << "status code=" << statusCode.toInt();

	QVariant reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
	if (reason.isValid())
		qDebug() << "reason=" << reason.toString();

	QNetworkReply::NetworkError err = reply->error();
	if (err != QNetworkReply::NoError) 
	{
		qDebug() << "Failed: " << reply->errorString();
	}
	else 
	{
		// 获取返回内容
		QByteArray array = reply->readAll();

		QJsonParseError json_error;
		QJsonDocument jsonDoc(QJsonDocument::fromJson(array, &json_error));

		if (json_error.error != QJsonParseError::NoError)
		{
			qDebug() << "json error!";
			return;
		}

		m_detailJson = jsonDoc.object();
		QJsonObject data = m_detailJson.find("data")->toObject();
		
		if (data.contains("ep_list"))
		{
			m_comicName = data.value("title").toString();

			QJsonArray subArray = data.value("ep_list").toArray();
			for (int i = 0; i < subArray.size(); i++)
			{
				QJsonObject epJson = subArray.at(i).toObject();

				if (epJson.contains("short_title"))
				{
					QString strTitle = epJson.value("short_title").toString();
					int strID = epJson.value("id").toInt();

					m_epMap[strID] = strTitle;
				}
			}
		}

		emit onDetailFinished();
	}
}

//解析下载所有章节
void BIlibiliComic::parsePerEP()
{
	QMap<int, QString>::iterator it = m_epMap.begin();

	for (it ; it != m_epMap.end(); ++it)
	{
		int nComicEpId = it.key();
		m_currentEP = m_epMap[nComicEpId];
		getIndexDataToken(QString::number(nComicEpId));
	}
}

//找到安卓缓存中的对应index.dat文件
QByteArray BIlibiliComic::findIndexDataFromAndroid(QString qstrPath)
{
	QByteArray data;

	QDir dir(qstrPath);

	if (!dir.exists())
		return data;

	dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

	QFileInfoList fileList = dir.entryInfoList();

	int fileCount = fileList.count();
	for (int i = 0; i < fileCount; i++)
	{
		QFileInfo fileInfo = fileList[i];
		if (fileInfo.isFile())
		{
			QString suffix = fileInfo.suffix();
			if (QString::compare(suffix, QString(tr("dat")), Qt::CaseInsensitive) == 0)
			{
				/*获取文件绝对路径即全路径*/
				QString filePath = fileInfo.absoluteFilePath();

				QFile file(filePath);
				file.open(QIODevice::ReadOnly);
				data = file.readAll();
				file.close();
			}
		}
	}

	return data;
}

//从安卓下载的缓存中直接读取
QByteArray BIlibiliComic::getIndexDataFromAndroid(QString qstrEpID)
{
	QByteArray arrayIndexData;

	QDir dir(m_strIndexDataPath);

	if (!dir.exists())
	{
		QMessageBox::warning(this, QString::fromLocal8Bit("提示"), QString::fromLocal8Bit("导出路径为空为空！"), QString::fromLocal8Bit("确定"));
		return arrayIndexData;
	}

	dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

	QFileInfoList fileList = dir.entryInfoList();

	int fileCount = fileList.count();
	for (int i = 0; i < fileCount; i++)
	{
		QFileInfo fileInfo = fileList[i];

		if (fileInfo.isDir())
		{
			QString strAbsolutePath = fileInfo.filePath();

			if (strAbsolutePath.contains(qstrEpID))
			{
				arrayIndexData = findIndexDataFromAndroid(strAbsolutePath);
			}
		}
	}

	return arrayIndexData;
}

//从服务器上获取index.dat
QByteArray BIlibiliComic::getIndexDataFromWeb(QString qstrEpID)
{
	QByteArray array;

	QString qstrRequset = "{\"ep_id\":" + qstrEpID + "}";

	QByteArray arrayPara(qstrRequset.toUtf8());
	QUrl url("https://manga.bilibili.com/twirp/comic.v1.Comic/GetImageIndex?device=pc&platform=web");
	QNetworkRequest request(url);
	//request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/x-www-form-urlencoded"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));

	QNetworkAccessManager* naManager = new QNetworkAccessManager(this);

	//同步post获得indexdata的token信息
	QNetworkReply* reply = naManager->post(request, arrayPara);
	QEventLoop eventLoop;
	connect(naManager, SIGNAL(finished(QNetworkReply*)), &eventLoop, SLOT(quit()));
	eventLoop.exec();       //block until finish

	QByteArray toKenArray = reply->readAll();

	QJsonParseError json_error;
	QJsonDocument jsonDoc(QJsonDocument::fromJson(toKenArray, &json_error));

	if (json_error.error != QJsonParseError::NoError)
	{
		qDebug() << "json error!";
		return array;
	}

	QJsonObject json = jsonDoc.object();
	QJsonObject data = json.find("data")->toObject();

	if (data.contains("path"))
	{
		QString qstrDataIndexToken = data.value("path").toString();
		QString qstrHost = data.value("host").toString();

		QString qstrRequest = qstrHost + qstrDataIndexToken;
		QUrl url(qstrRequest);
		QNetworkRequest request(url);
		request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));
		//QNetworkAccessManager* naManager = new QNetworkAccessManager(this);

		QNetworkReply* indexReply = naManager->get(request);

		//QEventLoop eventLoop;
		connect(naManager, SIGNAL(finished(QNetworkReply*)), &eventLoop, SLOT(quit()));
		eventLoop.exec();       //block until finish

		array = indexReply->readAll();
	}
	
	return array;
}

//现在获得index.dat,不从网络获取，而是从基于安卓下载的的缓存中读取。
void BIlibiliComic::getIndexDataToken(QString qstrEpID)
{
	QByteArray array;

	if (m_fromCache)
	{
		array = getIndexDataFromAndroid(qstrEpID);
	}
	else
	{
		array = getIndexDataFromWeb(qstrEpID);
	}

	int nTotal = array.size();

	//跳9个字节
	unsigned char* indexData = (uchar*)array.data();
	indexData += 9;
	int nBufferSize = nTotal - 9;

	int nComicID = m_comicID.toInt();
	int nEpID = qstrEpID.toInt();

	int* hashKey = generateHashKey(nComicID, nEpID);

	unhashContent(hashKey, indexData, nBufferSize);

	QByteArray indexDataJson = QByteArray((char*)indexData, nTotal - 9);

	QFile writeFile("./index.temp");

	if (writeFile.exists())
		writeFile.remove();

	writeFile.open(QIODevice::WriteOnly);
	writeFile.write(indexDataJson);
	writeFile.close();

	//解压
	unZip();

	QFile readFile("./index.dat");

	readFile.open(QIODevice::ReadOnly);
	QByteArray realData = readFile.readAll();
	readFile.close();
	parseAllImage(realData);

}

//解析下载所有图片
void BIlibiliComic::parseAllImage(QByteArray& realData)
{
	QJsonParseError json_error;
	QJsonDocument jsonDoc(QJsonDocument::fromJson(realData, &json_error));

	if (json_error.error != QJsonParseError::NoError)
	{
		qDebug() << "json error!";
		return;
	}

	QJsonObject imageJson = jsonDoc.object();

	QStringList list;

	if (imageJson.contains("pics"))
	{
		QJsonArray jsonArray = imageJson.value("pics").toArray();

		for (int i = 0; i < jsonArray.size(); i++)
		{
			QString image = jsonArray.at(i).toString();
			list.push_back(image);
		}
	}

	for (int i = 0;i < list.size();++i)
	{
		//请求到Token
		QByteArray imageToken = getImageToken(list[i]);

		//用Token请求图片
		QByteArray realImageData = getImageData(imageToken);

		saveTo(realImageData,i);
	}
}

//创建多级目录
QString mkMutiDir(const QString path) 
{
	QDir dir(path);
	if (dir.exists(path))
	{
		return path;
	}
	QString parentDir = mkMutiDir(path.mid(0, path.lastIndexOf('/')));
	QString dirname = path.mid(path.lastIndexOf('/') + 1);
	QDir parentPath(parentDir);
	if (!dirname.isEmpty())
		parentPath.mkpath(dirname);
	return parentDir + "/" + dirname;
}

//保存到文件
void BIlibiliComic::saveTo(QByteArray imageData,int name)
{
	QString strPath = m_saveRoot+ m_comicName + "/" + m_currentEP;
	QDir dir(strPath);
	if (!dir.exists())
		mkMutiDir(strPath);

	QString imageName = QString::number(name);
	QString imagePath = strPath + "/" + imageName + ".jpg";
	QFile writeFile(imagePath);

	if (writeFile.exists())
		return;

	std::cout << "保存：" << imagePath.toLocal8Bit().data() << std::endl;

	writeFile.open(QIODevice::WriteOnly);
	writeFile.write(imageData);
	writeFile.close();
}

//下载图片数据
QByteArray BIlibiliComic::getImageData(QByteArray& imageToken)
{
	QJsonParseError json_error;
	QJsonDocument jsonDoc(QJsonDocument::fromJson(imageToken, &json_error));

	if (json_error.error != QJsonParseError::NoError)
	{
		qDebug() << "json error!";
		return QByteArray();
	}

	QJsonObject imageJson = jsonDoc.object();

	QJsonObject imageTokenJson;
	if (imageJson.contains("data"))
	{
		QJsonArray subArray = imageJson.value("data").toArray();
		for (int i = 0; i < subArray.size(); i++)
		{
			imageTokenJson = subArray.at(i).toObject();
		}
	}

	if (imageTokenJson.contains("url"))
	{
		QString strUrl = imageTokenJson.value("url").toString();
		QString strToken = imageTokenJson.value("token").toString();


		QString qstrRequest = strUrl + "?token=" + strToken;
		QUrl url(qstrRequest);
		QNetworkRequest request(url);
		request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));

		QNetworkAccessManager* naManager = new QNetworkAccessManager(this);
		QNetworkReply* indexReply = naManager->get(request);

		QEventLoop eventLoop;
		connect(naManager, SIGNAL(finished(QNetworkReply*)), &eventLoop, SLOT(quit()));
		eventLoop.exec();       //block until finish

		QByteArray array = indexReply->readAll();

		return array;
	}

	return QByteArray();
}

//pos请求图片Token
QByteArray BIlibiliComic::getImageToken(QString imageinfo)
{
	QString qstrRequset = "{\"urls\":\"[\\\"" + imageinfo + "\\\"]\"}";

	QByteArray array(qstrRequset.toUtf8());
	QUrl url("https://manga.bilibili.com/twirp/comic.v1.Comic/ImageToken?device=pc&platform=web");
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));

	QNetworkAccessManager* naManager = new QNetworkAccessManager(this);

	QNetworkReply* indexReply = naManager->post(request, array);

	QEventLoop eventLoop;
	connect(naManager, SIGNAL(finished(QNetworkReply*)), &eventLoop, SLOT(quit()));
	eventLoop.exec();       //block until finish

	QByteArray imageToken = indexReply->readAll();

	return imageToken;
}

//解压
void BIlibiliComic::unZip()
{
	unz_file_info64 FileInfo;
	unzFile zFile = unzOpen64("./index.temp");
	if (NULL == zFile)
		qDebug() << "zero!";
	else
		qDebug() << "haha";

	unz_global_info64 gi;

	if (unzGetGlobalInfo64(zFile, &gi) != UNZ_OK)
	{
		qDebug() << "ho no";
	}

	int result;
	for (int i = 0; i < gi.number_entry; ++i)
	{
		char file[256] = { 0 };
		char ext[256] = { 0 };

		char com[1024] = { 0 };
		if (unzGetCurrentFileInfo64(zFile, &FileInfo, file, sizeof(file), ext, 256, com, 1024) != UNZ_OK)
			qDebug() << "error";
		qDebug() << file << ":" << ext << ":" << com;

		if (!(FileInfo.external_fa & FILE_ATTRIBUTE_DIRECTORY)) //文件，否则为目录

		//打开文件
		result = unzOpenCurrentFile(zFile);/* 无密码 */
		//result = unzOpenCurrentFilePassword(zFile, "szPassword"); /* 有密码 */

		char data[1024] = { 0 };
		int size;
		//读取内容

		QString path = QString("./") + file;
		QFile f(path);

		if (f.exists())
			f.remove();

		f.open(QFile::WriteOnly);
		while (true) 
		{
			size = unzReadCurrentFile(zFile, data, sizeof(data));

			if (size <= 0)
				break;

			//QString str;
			//str = QString::fromLocal8Bit(data, size);
			f.write(data, size);
		}

		f.close();

		//关闭当前文件
		unzCloseCurrentFile(zFile);

		//出错
		if (i < gi.number_entry - 1 && unzGoToNextFile(zFile) != UNZ_OK)
			qDebug() << "error2";
	}
	unzClose(zFile);

}

//反hash
void BIlibiliComic::unhashContent(int* hashKey, unsigned char* indexData,int nDataSize)
{
	for (int i = 0; i < nDataSize; ++i)
	{
		indexData[i] ^= hashKey[i % 8];
	}
}

//计算hashkey
int* BIlibiliComic::generateHashKey(int nComicID, int nEpID)
{
	int* n = new int[8];
	int e = nComicID;
	int	t = nEpID;
	n[0] = t;
	n[1] = t >> 8;
	n[2] = t >> 16;
	n[3] = t >> 24;
	n[4] = e;
	n[5] = e >> 8;
	n[6] = e >> 16;
	n[7] = e >> 24;

	for (int i = 0;i < 8;i++)
	{
		n[i] = n[i] % 256;
	}

	return n;
}

//pos请求获得漫画详细信息
void BIlibiliComic::getComicDetail(QUrl url)
{
	QString qtrRequest = "{\"comic_id\":" + m_comicID + "}";
	QByteArray array(qtrRequest.toUtf8());
	QNetworkRequest request(url);
	//request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/x-www-form-urlencoded"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));

	QNetworkAccessManager* naManager = new QNetworkAccessManager(this);
	QMetaObject::Connection connRet = QObject::connect(naManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(requestDetailFinished(QNetworkReply*)));

	naManager->post(request, array);
}

BIlibiliComic::BIlibiliComic(QWidget *parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);

	QUrl url("https://manga.bilibili.com/twirp/comic.v2.Comic/ComicDetail?device=pc&platform=web");

	//保存目录
	m_saveRoot = "G:/";

	//index.dat文件目录，从安卓上下载下来的缓存
	m_strIndexDataPath = "G:/25971";

	//漫画ID
	m_comicID = "25523";

	//从安卓缓存中提取还是从web下载index.dat
	m_fromCache = false;

	//获取漫画详情和章节信息
	getComicDetail(url);
}
