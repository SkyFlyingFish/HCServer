#include "MyServer.h"
#include "Def.h"
#include <curl/curl.h>
#include <QCryptographicHash>
#include <QUuid>

MyServer::MyServer(QObject *parent) : QObject(parent)
{
#if USE_HTTPS
    _server = new HttpsServer;
    // 设置CA证书
    QFile key(":/key.pem");
    key.open(QIODevice::ReadOnly);
    _server->setPrivateKey(QSslKey(&key, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey,
                                   "123456"));

    QFile cert(":/cert.pem");
    cert.open(QIODevice::ReadOnly);
    _server->setLocalCertificate(QSslCertificate(&cert));
#else
    _server = new HttpServer;
#endif
    if(!_server->listen(QHostAddress::Any, 10251))
    {
        qDebug() << "listen error";
        exit(1);
    }
    else
    {
        qDebug() << "listen ok";
    }

    connect(_server, SIGNAL(requestReady(Tufao::HttpServerRequest&,Tufao::HttpServerResponse&)),
            this, SLOT(slotRequestReady(Tufao::HttpServerRequest &, Tufao::HttpServerResponse&)));
}

void MyServer::handleRequestReady(HttpServerRequest &request, HttpServerResponse &response)
{
    // 得到请求POST数据，应该是Json格式的报文
    QByteArray data = request.readBody();

    qDebug() << "client send data:" << data;

    QJsonDocument doc = QJsonDocument::fromJson(data);

    // JSON对象 {cmd:login, username:aa, password:bb}
    QJsonObject reqObj = doc.object();

    QJsonObject respObj = handle(reqObj);

    QByteArray respData = QJsonDocument(respObj).toJson();

    response.writeHead(HttpResponseStatus::OK);
    response.end(respData);
}

/*
    cmd = login, cmd = reg
*/
QJsonObject MyServer::handle(QJsonObject reqObj)
{
    QString cmd = reqObj.value(HC_CMD).toString();
    QJsonObject resp;

    if(cmd == HC_LOCATION_CHANGE)
    {
        resp = handleLocationChange(reqObj);
    }
    else if(cmd == HC_START_ORDER)
    {
        // 1. 向数据服务器发送请求，创建一个订单（订单保存在MySQL中）
        resp = handleStartOrder(reqObj);
        /*
            订单表结构：
            乘客用户名（请求订单时直接写入）
            司机用户名（由服务器安排司机，写入）
            订单号
            lng1
            lat1
            lng2
            lat2
            多少钱
            上车时间戳
            下车时间戳
        */
        //
    }
    else if(cmd == HC_LOGIN)
    {
        resp = handleLogin(reqObj);
    }
    else if(cmd == HC_REG)
    {
        resp = handleReg(reqObj);
    }


    return resp;
}

// 1 0 1 1 1 0 0 0 1 1 纬度
// 1 1 0 1 0 0 1 0 1 1 经度
// 11011 01110 00010 01111 20位的2进制值
// 11011 01110 00010 01111 (0~31)
// 27    14    2     15
// v     f     2     g  "vf2g"   "vf2h"

/* 查询数据 get/query 条件 */
QJsonObject MyServer::handleLogin(QJsonObject obj)
{
    QJsonObject resp;
    resp.insert(HC_RESULT, HC_OK);

    QString username = obj.value(HC_USERNAME).toString();
    QString password = obj.value(HC_PASSWORD).toString();
    password = md5(password);
    QString type = obj.value(HC_LOGINTYPE).toString();

    // 构造数据请求报文
    QJsonObject queryObj;
    queryObj.insert(HC_CMD, HC_QUERY);
    queryObj.insert(HC_OBJECT, HC_USER_TABLE);
    queryObj.insert(HC_USERNAME, username);


    QJsonObject queryResult = execute(queryObj);
    if(queryResult.value(HC_RESULT).toString() == HC_OK)
    {
        if(queryResult.value(HC_COUNT).toInt() == 1)
        {
            QJsonObject data = queryResult.value(HC_DATA).toArray().at(0).toObject();
            // 登陆成功
            if(data.value(HC_PASSWORD).toString() == password)
            {
                QString uuid = QUuid::createUuid().toString();
                resp.insert(HC_SESSION, uuid);

                // 产生这次登陆session，并保存在数据服务器
                // 要求数据服务器产生session
                QJsonObject insertObj;
                insertObj.insert(HC_CMD, HC_INSERT);
                insertObj.insert(HC_OBJECT, HC_SESSION);
                insertObj.insert(HC_USERNAME, username);
                insertObj.insert(HC_SESSION, uuid);// session怎么产生
                insertObj.insert(HC_LOGINTYPE, type);
                insertObj.insert(HC_TYPE, HC_TEMP);

                // 让数据服务器执行增加Session的命令
                QJsonObject insertResp = execute(insertObj);
                if(insertResp.value(HC_RESULT).toString() != HC_OK)
                {
                    resp.insert(HC_RESULT, HC_ERR);
                    resp.insert(HC_REASON, "add session error");
                }
            }
            else
            {
                resp.insert(HC_RESULT, HC_ERR);
                resp.insert(HC_REASON, "password error");
            }
        }
        else
        {
            resp.insert(HC_RESULT, HC_ERR);
            resp.insert(HC_REASON, "username error1");
        }
    }
    else
    {
        resp.insert(HC_RESULT, HC_ERR);
        resp.insert(HC_REASON, "username error");
    }

    return resp;
}

/* 增加一个记录 insert */
QJsonObject MyServer::handleReg(QJsonObject obj)
{
    QString username = obj.value(HC_USERNAME).toString();
    QString password = obj.value(HC_PASSWORD).toString();
    password = md5(password);
    QString mobile = obj.value(HC_MOBILE).toString();
    QString email = obj.value(HC_EMAIL).toString();
    QString id = obj.value(HC_ID).toString();

    QJsonObject insertObj;

    insertObj.insert(HC_CMD, HC_INSERT);
    insertObj.insert(HC_TYPE, HC_PERMANENT);
    insertObj.insert(HC_OBJECT, HC_USER_TABLE);
    QJsonArray arr;
    arr << username << password << mobile << email << id;
    insertObj.insert(HC_DATA, arr);

    /* 向dataServer发送插入数据请求 */
    return execute(insertObj);
}

QJsonObject MyServer::handleLocationChange(QJsonObject obj)
{
    // insert query update(modify)
    // locationchange
    obj.insert(HC_CMD, HC_UPDATE); // {cmd: locationchage}
    obj.insert(HC_OBJECT, HC_POSITION);

    /*
        {
            cmd: update,
            object: position,
            session: {xxxx-xxxxx-xxxxx-xxxxx},
            lng: 118.19098111,
            lat: 39.11882711
        }
    */
    return execute(obj);
}

QJsonObject MyServer::handleStartOrder(QJsonObject obj)
{
    // 1. 向数据服务器发送请求，创建一个订单（订单保存在MySQL中）
#if 0
    QString username = obj.value(HC_USERNAME).toString();
    QString password = obj.value(HC_PASSWORD).toString();
    password = md5(password);
    QString mobile = obj.value(HC_MOBILE).toString();
    QString email = obj.value(HC_EMAIL).toString();
    QString id = obj.value(HC_ID).toString();

    QJsonObject insertObj;

    insertObj.insert(HC_CMD, HC_INSERT);
    insertObj.insert(HC_TYPE, HC_PERMANENT);
    insertObj.insert(HC_OBJECT, HC_USER_TABLE);
    QJsonArray arr;
    arr << username << password << mobile << email << id;
    insertObj.insert(HC_DATA, arr);
#endif

    QJsonObject ret;
    ret.insert(HC_RESULT, HC_ERR);

    QString pid, did; // passengerID, driverID

    QString session = obj.value(HC_SESSION).toString();
    pid = getUserID(session);
    if(pid.length() == 0)
    {
        // session已经过期了
        ret.insert(HC_REASON, "get userid error");
        return ret;
    }

    QString lngs = obj.value(HC_START).toObject().value(HC_LNG).toString();
    QString lats = obj.value(HC_START).toObject().value(HC_LAT).toString();
    QString lnge = obj.value(HC_END).toObject().value(HC_LNG).toString();
    QString late = obj.value(HC_END).toObject().value(HC_LAT).toString();

    did = getNearbyDriverID(lngs, lats);
    if(did.length() == 0)
    {
        // 附近没有司机
        ret.insert(HC_REASON, "no driver neerby");
        return ret;
    }

    QJsonObject insertObj;
    insertObj.insert(HC_CMD, HC_INSERT);
    insertObj.insert(HC_TYPE, HC_PERMANENT);
    insertObj.insert(HC_OBJECT, HC_ORDER);
    QJsonArray arr;

    arr << pid << did << QUuid::createUuid().toString() << lngs << lats << lnge << late;
    insertObj.insert(HC_DATA, arr);

    return execute(insertObj);

    /*
        订单表结构：
        乘客用户名（请求订单时直接写入）
        司机用户名（由服务器安排司机，写入）
        订单号
        lng1
        lat1
        lng2
        lat2
        多少钱
        上车时间戳
        下车时间戳
    */
}

QString MyServer::getUserID(QString sessionid)
{
    /*
        insert, update, get

        {
            cmd: get
            object: userid
            session: sessionid
        }

        {
            result: ok
            userid: xxxxxxx
        }

        {
            result: err,
            reason: xxxxx
        }
    */

    QJsonObject req;
    req.insert(HC_CMD, HC_GET);
    req.insert(HC_OBJECT, HC_USERID);
    req.insert(HC_SESSION, sessionid);

    QJsonObject resp = execute(req);
    if(resp.value(HC_RESULT).toString() == HC_OK)
        return resp.value(HC_USERID).toString();

    return QString();
}

QString MyServer::getNearbyDriverID(QString lng, QString lat)
{
    QJsonObject req;
    req.insert(HC_CMD, HC_GET);
    req.insert(HC_OBJECT, HC_NEARBY_DRIVER);
    req.insert(HC_LNG, lng);
    req.insert(HC_LAT, lat);

    QJsonObject resp = execute(req);
    /*
        {
            result: ok
            userid: xxxxxxx
        }
    */
    if(resp.value(HC_RESULT).toString() == HC_OK)
    {
        return resp.value(HC_USERID).toString();
    }

    return QString();
}

ssize_t MyServer::curlCallback(char *ptr, int m, int n, void *arg)
{
    QByteArray& respBuffer = *(QByteArray*)arg;
    respBuffer.append(ptr, m*n);
    return m*n;
}

QString MyServer::md5(QString value)
{
    /* 128/8 = 16 */
    /* 0x1234F6A5 "1234F6A5" */
    QByteArray bb = QCryptographicHash::hash ( value.toUtf8(), QCryptographicHash::Md5 );
    // 把二进制转换成字符串，32个字节
    return bb.toHex();
}

QJsonObject MyServer::execute(QJsonObject obj)
{
    QByteArray insertBuf = QJsonDocument(obj).toJson();

    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, HC_URL_DATA);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, insertBuf.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, insertBuf.size());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);
    QByteArray respBuffer;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respBuffer);

    curl_easy_perform(curl);

    return QJsonDocument::fromJson(respBuffer).object();
}

void MyServer::slotRequestReady(HttpServerRequest &request, HttpServerResponse& response)
{
    // 处理POST数据
    connect(&request, &HttpServerRequest::end, [&](){
        handleRequestReady(request, response);
    });
}
