#include <QtCore>
#include <QtNetwork>
#include <cassert>
#include <functional>
#include <iostream>

bool postJson( const QString &, const QJsonObject &, const QString &, QJsonDocument &,
               QString &, const std::function<bool()> & );
bool runPythonProcess( const QString &, const QString &, const QStringList &, const QString &,
                       QString &, QString &, const std::function<bool()> & );

int main( int argc, char **argv )
{
  QCoreApplication app( argc, argv );
  assert( argc == 2 );
  QTcpServer server;
  assert( server.listen( QHostAddress::LocalHost, 0 ) );
  const QString url = QStringLiteral( "http://127.0.0.1:%1/" ).arg( server.serverPort() );
  QString error;
  QJsonDocument response;
  bool connected = false;
  auto connection = QObject::connect( &server, &QTcpServer::newConnection, &server, [&] {
    connected = true;
    auto *socket = server.nextPendingConnection();
    QObject::connect( socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater );
    // Hold the request open: no response until the client cancels it.
  } );
  assert( !postJson( url, {}, "test", response, error, [] { return true; } ) );
  assert( !connected );
  assert( error == QStringLiteral( "Prediction stopped." ) );

  bool canceled = false;
  QTimer::singleShot( 150, [&] { canceled = true; } );
  QElapsedTimer elapsed;
  elapsed.start();
  assert( !postJson( url, {}, "test", response, error, [&] { return canceled; } ) );
  assert( connected && canceled && elapsed.elapsed() < 2000 );
  assert( response.isNull() && error == QStringLiteral( "Prediction stopped." ) );
  QObject::disconnect( connection );

  QObject::connect( &server, &QTcpServer::newConnection, &server, [&] {
    auto *socket = server.nextPendingConnection();
    QObject::connect( socket, &QTcpSocket::readyRead, socket, [socket] {
      socket->readAll();
      const QByteArray body = "{\"ok\":true,\"result\":[]}";
      socket->write( "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number( body.size() )
                     + "\r\nConnection: close\r\n\r\n" + body );
      socket->flush();
    } );
    QObject::connect( socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater );
  } );
  canceled = false;
  error.clear();
  const bool success = postJson( url, {}, "test", response, error, [&] { return canceled; } );
  if ( !success ) std::cerr << error.toStdString() << '\n';
  assert( success );
  assert( response.object().value( "ok" ).toBool() && error.isEmpty() );

  const QString python = QString::fromLocal8Bit( argv[1] );
  QTemporaryDir temp;
  assert( temp.isValid() );
  const QString script = temp.filePath( "predict.py" );
  QFile file( script );
  assert( file.open( QIODevice::WriteOnly ) );
  file.write( "import pathlib, time\npathlib.Path('started').touch()\ntime.sleep(1)\npathlib.Path('finished').touch()\nprint('{}')\n" );
  file.close();
  QString output;
  assert( !runPythonProcess( script, python, { script }, "test", output, error, [] { return true; } ) );
  assert( !QFileInfo::exists( temp.filePath( "started" ) ) );
  canceled = false;
  QTimer::singleShot( 250, [&] { canceled = true; } );
  elapsed.restart();
  assert( !runPythonProcess( script, python, { script }, "test", output, error, [&] { return canceled; } ) );
  assert( canceled && elapsed.elapsed() < 2000 );
  assert( QFileInfo::exists( temp.filePath( "started" ) ) );
  assert( output.isEmpty() && error == QStringLiteral( "Prediction stopped." ) );
  // A killed child must not finish its delayed work after the UI resumes.
  QEventLoop wait;
  QTimer::singleShot( 1200, &wait, &QEventLoop::quit );
  wait.exec();
  assert( !QFileInfo::exists( temp.filePath( "finished" ) ) );
  canceled = false;
  error.clear();
  assert( runPythonProcess( script, python, { script }, "test", output, error, [&] { return canceled; } ) );
  assert( output == QStringLiteral( "{}" ) && error.isEmpty() );
  std::cout << "Inference transport: remote/local stop, no late output, and retry PASS\n";
}
