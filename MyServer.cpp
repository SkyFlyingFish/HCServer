#include "MyServer.h"
#include "Def.h"

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

QJsonObject MyServer::handle(QJsonObject reqObj)
{
    QJsonObject resp;
    resp.insert(HC_RESULT, HC_OK);
    return resp;
}

void MyServer::slotRequestReady(HttpServerRequest &request, HttpServerResponse& response)
{
    // 处理POST数据
    connect(&request, &HttpServerRequest::end, [&](){
        handleRequestReady(request, response);
    });
}
