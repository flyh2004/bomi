#ifndef APP_X11_HPP
#define APP_X11_HPP

#include <QtGui/QWidget>

#ifdef Q_WS_X11

class AppX11 : public QObject {
	Q_OBJECT
public:
	AppX11(QObject *parent = 0);
	~AppX11();
	void setScreensaverDisabled(bool disabled);
	void setAlwaysOnTop(WId wid, bool onTop);
	QStringList devices() const;
private slots:
	void ss_reset();
private:
	struct Data;
	Data *d;
};

#endif

#endif // APPLICATION_X11_HPP
