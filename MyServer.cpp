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
    if(cmd == HC_LOGIN)
    {
        resp = handleLogin(reqObj);
    }
    if(cmd == HC_REG)
    {
        resp = handleReg(reqObj);
    }

    return resp;
}

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

    // 将报文发送给数据服务器
    /*
     * /*
        {
            result: ok,
            count: 1,
            data: [
                {
                    username: xxx,
                    password: yyy,
                    email: zzz,
                    id: yyy,
                    mobile: yyy
                },
                {

                }

            ]
        }

     * }
     */
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
