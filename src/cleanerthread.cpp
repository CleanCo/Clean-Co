#include <QDir>
#include <QFileInfo>
#include <QDomDocument>
#include <QTime>
#include <QtDebug>

#include "cleanerthread.h"

CleanerThread::CleanerThread(ToThread args, QObject *parent) :
    QObject(parent)
{
    arguments = args;
    proc = new QProcess;
    connect(proc,SIGNAL(readyReadStandardOutput()),this,SLOT(readyRead()));
    connect(proc,SIGNAL(readyReadStandardError()),this,SLOT(readyReadError()));
    connect(proc,SIGNAL(finished(int)),this,SLOT(finished(int)));
}

void CleanerThread::startNext(const QString &inFile, const QString &outFile)
{
    cleaningTime = QTime::currentTime();

    scriptOutput.clear();
    outSVG = QString(outFile).replace("svgz","svg");
    currentIn = inFile;
    currentOut = outFile;

    QString str = prepareFile(inFile);
    if (str.isEmpty()) { // == bad svg file
        emit cleaned(info());
        return;
    }
    QDir().mkpath(QFileInfo(outFile).absolutePath());
    QFile file(outSVG);
    if (file.open(QFile::WriteOnly)) {
        QTextStream out(&file);
        out<<str;
    }

    QStringList args;
    args.append(arguments.cleanerPath);
    args.append("--in-file="+outSVG);
    args.append("--out-file="+outSVG);
    args.append(arguments.args);
    proc->start(arguments.perlPath,args);
}

// remove attribute xml:space before starting a script,
// in the same time copy svg source to the new path
QString CleanerThread::prepareFile(const QString &file)
{
    QDomDocument inputDom;
    if (QFileInfo(file).suffix() == "svg") {
        QFile inputFile(file);
        if (inputFile.open(QFile::ReadOnly))
            inputDom.setContent(&inputFile);
    } else {
        QProcess proc;
        QStringList args;
        args<<"e"<<"-so"<<file;
        proc.start(arguments.zipPath,args);
        proc.waitForFinished();
        inputDom.setContent(&proc);
    }
    QDomNodeList nodeList = inputDom.childNodes();
    for (int i = 0; i < nodeList.count(); ++i) {
        if (nodeList.at(i).nodeName().contains(QRegExp("svg|svg:svg"))) {
//            (width || height ) && !viewBox
            QDomElement element = nodeList.at(i).toElement();
            element.removeAttribute("xml:space");
            QRegExp rx("px|pt|pc|mm|cm|m|in|ft|em|ex|%");
            QString width = element.attribute("width");
            width.remove(rx);
            QString height = element.attribute("height");
            height.remove(rx);
            if (width.toInt() < 0 || height.toInt() < 0)
                return "";
            else if ((element.attribute("width").contains("%")
                     || element.attribute("height").contains("%"))
                      && element.attribute("viewBox").isEmpty())
                return "";
            else if (width.toInt() == 0 || height.toInt() == 0)
                element.setAttribute("viewBox","0 0 0 0");
            return inputDom.toString();
        }
    }
    return "";
}

void CleanerThread::readyRead()
{
    scriptOutput += proc->readAllStandardOutput();
}

void CleanerThread::readyReadError()
{
    QString error = proc->readAllStandardError();
    if (error.contains("Can't locate XML/Twig.pm in"))
        emit criticalError(tr("You have to install XML-Twig."));
    else
        qDebug()<<error<<tr("in file")<<currentIn;
}

void CleanerThread::finished(int)
{
    if (QFileInfo(currentOut).suffix() == "svgz") {
        if (currentOut == currentIn) // because 7zip can't overwrite the svgz, for some reason
            QFile(currentIn).remove();
        QFile(currentOut).remove(); // if it's already exist

        QProcess procZip;
        QStringList args;
        args<<"a"<<"-tgzip"<<"-mx"+arguments.level<<currentOut<<outSVG;
        procZip.start(arguments.zipPath,args);
        procZip.waitForFinished();
        QFile(outSVG).remove();
    }
    emit cleaned(info());
}

// generate files info
SVGInfo CleanerThread::info()
{
    SVGInfo info;
    info.paths<<currentIn<<currentOut;
    info.elemInitial = findValue("The initial number of elements is");
    info.elemFinal = findValue("The final number of elements is");
    info.attrInitial = findValue("The initial number of attributes is");
    info.attrFinal = findValue("The final number of attributes is");

    // if svgz saved to svg with cleaning, we need to save size of uncompressed/uncleaned svg
    if ((QFileInfo(currentOut).suffix() == "svg"
        && QFileInfo(currentIn).suffix() == "svgz")
        || (currentIn == currentOut)) {

        info.sizes<<findValue("The initial file size is");
        info.compress = ((float)QFileInfo(currentOut).size()
                               /findValue("The initial file size is"))*100;
    } else {
        info.sizes<<QFileInfo(currentIn).size();
        info.compress = ((float)QFileInfo(currentOut).size()
                               /QFileInfo(currentIn).size())*100;
    }
    if (info.compress > 100)
        info.compress = 100;
    else if (info.compress < 0)
        info.compress = 0;

    info.sizes<<QFileInfo(currentOut).size();

    info.time = cleaningTime.elapsed();
    info.crashed = scriptOutput.isEmpty(); // true - mean crash

    if (arguments.args.contains("--quiet=no"))
        qDebug()<<scriptOutput;

    return info;
}

// find values in svgcleaner output
int CleanerThread::findValue(const QString &text)
{
    if (!scriptOutput.contains(text))
        return 0;
    QString tmpStr = scriptOutput.split("\n").filter(text).first();
    return tmpStr.remove(QRegExp("[A-Za-z]|%| ")).toInt();
}
