#include "Backend.hpp"

// TODO:
// - Think about all

using namespace Instances;

/**
 * @brief Backend::Backend
 * @param parent
 *
 * The constructor of the Backend class.
 * Initializes the editor window list and the thread list as well
 * as the settings backend.
 */
Backend::Backend(QObject *parent) : QObject(parent){
    QApplication::setStyle(SettingsBackend::getSettingsFor("Design", "").toString());
}

/**
 * @brief Backend::~Backend
 *
 * The destructor of the Backend class.
 * Eliminates all the threads that were orphaned
 * when all the windows closed.
 */
Backend::~Backend(){
    for(auto* thread: threads.values()){
        if(thread){
            if(thread->isRunning())
                thread->terminate();
            delete thread;
        }
    }
}

/**
 * @brief Backend::addChild
 * @param instance
 * @param removeSettings
 *
 * Is called by one of the editor window instances;
 * enlists the child and creates an empty thread entry
 * in the list so that the two correlate.
 */
void Backend::addInstance(IInstance *instance, bool removeSettings){
    int id = instance->ID;
    if(instances.contains(id))
        return;
    if(removeSettings)
        SettingsBackend::removeSettings(id);
    instances.insert(id, instance);
    connect(instance, SIGNAL(closing(IInstance*)),  this, SLOT(instanceClosing(IInstance *)));
    connect(instance, SIGNAL(destroyed(QObject*)),  this, SLOT(instanceDestroyed(QObject *)));
    connect(instance, SIGNAL(runCode(IInstance*)),  this, SLOT(instanceRunCode(IInstance *)));
    connect(instance, SIGNAL(stopCode(IInstance*)), this, SLOT(instanceStopCode(IInstance *)));
    connect(instance, SIGNAL(changeSetting (IInstance*, const QString, const QVariant&)),
            this, SLOT(instanceChangedSetting(IInstance*, const QString&, const QVariant&)));
    connect(instance, SIGNAL(getSetting(IInstance*, const QString, QVariant&)),
            this, SLOT(instanceRequestSetting(IInstance*, const QString&, QVariant&)));
    connect(instance, SIGNAL(changeSettings(IInstance*, const QHash<QString,QVariant>&)),
            this, SLOT(instanceChangedSettings(IInstance*, const QHash<QString,QVariant>&)));
    connect(instance, SIGNAL(getSettings(IInstance*, QHash<QString,QVariant>&)),
            this, SLOT(instanceRequestSettings(IInstance*, QHash<QString,QVariant>&)));
    connect(instance, SIGNAL(closeAll()), this, SLOT(childSaidCloseAll()));
    connect(instance, SIGNAL(openSettings(IInstance*)), this, SLOT(settingsWindowRequested(IInstance*)));
    connect(instance, SIGNAL(openHelp(IInstance*)), this, SLOT(openHelp(IInstance*)));
    ids.append(id);
    saveIDs();
}

/**
 * @brief Backend::nextID
 * @return Free to use id
 *
 * Look up the first free ID for a new Instance.
 */
int Backend::nextID(){
    auto id = 0;
    while(ids.contains(id))
        ++id;
    return id;
}

/**
 * @brief Backend::loadIds
 * @return A list of used IDs
 *
 * Return the list of ids for which settings
 * should exist.
 */
QList<int> Backend::loadIds()
{
    auto ids = SettingsBackend::getSettingsFor("Instances", QVariantList()).toList();
    QList<int> res;
    for(const auto id : ids){
        bool ok;
        int i = id.toInt(&ok);
        if(ok)
            res.append(i);
    }
    return res;
}

/**
 * @brief Backend::instanceClosing
 * @param instance
 *
 * Reacts to the closing signal and calls the
 * removeInstance() routine.
 * TODO: Needed?
 */
void Backend::instanceClosing(IInstance *instance)
{
    removeInstance(instance);
}

/**
 * @brief Backend::instanceDestroyed
 * @param instance
 *
 * Reacts to the destroyed signal and removes
 * the instance from the backends' memory.
 */
void Backend::instanceDestroyed(QObject *instance)
{
    auto id = ((IInstance*)instance)->ID;
    instances.remove(id);
    removeInstance(id, false);
}

/**
 * @brief Backend::removeChild
 * @param child
 *
 * Is called by one of the editor window instances;
 * removes the child from the list and closes the thread.
 * Removes all the settings that belong to the current child.
 * BUG: When the settings of the next children are updated,
 *      the settings window will display the settings of the
 *      killed child. This will result in confusion of the user.
 * TODO: Fix bug!
 */
bool Backend::removeInstance(Instances::IInstance *instance, bool removeSettings){
    return removeInstance(instance->ID, removeSettings);
}

bool Backend::removeInstance(int id, bool removeSettings){
    if(instances.contains(id)){
        disconnect(instances[id], SIGNAL(destroyed(QObject*)), this, SLOT(instanceDestroyed(QObject*)));
        if(!instances[id]->close()){
            connect(instances[id], SIGNAL(destroyed(QObject*)), this, SLOT(instanceDestroyed(QObject*)));
            return false;
        }
        instances[id]->deleteLater();
        instances.remove(id);
    }
    terminateThread(id);
    if(removeSettings && ids.size() > 1){
        SettingsBackend::removeSettings(id);
        ids.removeOne(id);
        saveIDs();
    }
    return true;
}

/**
 * @brief Backend::childSaidCloseAll
 *
 * Is called by one of the editor window instances;
 * when a user requests to exit the application, this
 * will tell all the children to terminate.
 */
void Backend::childSaidCloseAll(){
    auto notRemoved = ids;
    for(const auto id : ids){
        disconnect(instances[id], SIGNAL(destroyed(QObject*)), this, SLOT(instanceDestroyed(QObject*)));
        if(removeInstance(id, false))
            notRemoved.removeOne(id);
    }
    if(!notRemoved.empty()){
        for(const auto id : ids)
            if(!notRemoved.contains(id))
               SettingsBackend::removeSettings(id);
        ids = notRemoved;
    }
    saveIDs();
}

/**
 * @brief Backend::childExited
 * @param child
 * @param file
 *
 * Is called by one of the editor window instances;
 * when the child reacts to the closedAction, it is removed
 * from the list.
 */
void Backend::childExited(IInstance *child, QString file){
    Q_UNUSED(child);
    Q_UNUSED(file);
//    saveSettings(child, file);
//    children.removeOne(child);
}

/**
 * @brief Backend::getSettings
 * @param instance
 * @return a list of current settings
 *
 * Gets all settings for a specific window.
 */
QHash<QString, QVariant> Backend::getSettings(IInstance* instance)
{
    return getSettings(instance->ID);
}

/**
 * @brief Backend::getSettings
 * @param id
 * @return a QHash of all the settings for an id.
 *
 * looks up the settings for an editor window child.
 */
QHash<QString, QVariant> Backend::getSettings(int id)
{
    return SettingsBackend::getSettings(id);
}

/**
 * @brief Backend::settingsWindowRequested
 * @param instance
 *
 * Creates a settings window instance.
 */
void Backend::settingsWindowRequested(IInstance *instance){
    SettingsWindow settingsWin(instance->ID);
    settingsWin.exec();
}

/**
 * @brief Backend::openHelp
 *
 * Opens a help window in HTML.
 */
void Backend::openHelp(IInstance *){
    QUrl url(directoryOf("rc").absoluteFilePath("help.html"));
    url.setScheme("file");
    QDesktopServices::openUrl(url);
}

/**
 * @brief Backend::directoryOf
 * @param subdir
 * @return the directory one wants to navigate into
 *
 * Platform independent wrapper to changing the directory.
 */
QDir Backend::directoryOf(const QString &subdir){
    QDir dir(QApplication::applicationDirPath());

#if defined(Q_OS_MAC)
        dir.cdUp();
        dir.cdUp();
        dir.cdUp();
#elif defined(Q_OS_WIN)
    dir.cdUp();
#endif
    if(dir.cd(subdir))
        return dir;
    else
        return QDir(dir.absolutePath() + subdir);
}

/**
 * @brief Backend::removeSettings
 * @param instance
 *
 * removes the settings for a specific file.
 */
void Backend::removeSettings(IInstance* instance){
    SettingsBackend::removeSettings(instance->ID);
}

void Backend::removeSettings(int id){
    SettingsBackend::removeSettings(id);
}

/**
 * @brief Backend::isLast
 * @return true if the child is the last one, false otherwise
 *
 * checks whether there is only one or no child in the list.
 */
bool Backend::isLast(){
    return ids.length() < 2;
}

/**
 * @brief Backend::instanceRunCode
 *
 * reacts to the run SIGNAL by running the code(duh) that is
 * in the editor at the moment.
 */
void Backend::instanceRunCode(IInstance *instance)
{
    auto id = instance->ID;
    if(threads.contains(id)){
        auto worked = threads[id]->updateCode(instance->title(), instance->sourceCode());
        if(!worked){
            // Dont't stop!
//            instances[id]->codeStopped();
            instances[id]->reportError(tr("Code is faulty."));
//            if(!threads[id]->isRunning())
//                terminateThread(id);
        }
    }else{
        bool ok;
        int compiler = SettingsBackend::getSettingsFor("UseCompiler", -1, (int)id).toInt(&ok);
        if(!ok)
            compiler = -1;
        switch(compiler){
            case 0:
                this->runPySoundFile(instance);
                break;
            case 1:
                this->runQtSoundFile(instance);
                break;
            case 2:
                this->runGlFile(instance);
                break;
            case 3:
                this->runPyFile(instance);
                break;
            default:
                instance->codeStopped();
                instance->reportError(tr("Compiler not found."));
        }
    }
}

/**
 * @brief Backend::instanceStopCode
 * @param instance
 *
 * Reacts to the stopCode signal of an instance.
 * Stops the executing context.
 */
void Backend::instanceStopCode(IInstance *instance)
{
    terminateThread(instance->ID);
}

/**
 * @brief Backend::instanceChangedSetting
 * @param instance
 * @param key
 * @param value
 *
 * Reacts to the instance changing settings.
 * Saves the new settings.
 */
void Backend::instanceChangedSetting(IInstance *instance, const QString &key, const QVariant &value)
{
    SettingsBackend::saveSettingsFor(instance->ID, key, value);
}

/**
 * @brief Backend::instanceRequestSetting
 * @param instance
 * @param key
 * @param value
 *
 * Reacts to the instance requesting its settings for
 * a given key.
 */
void Backend::instanceRequestSetting(IInstance *instance, const QString &key, QVariant &value)
{
    value = SettingsBackend::getSettingsFor(key, value, instance->ID);
}

QVariant Backend::getSetting(QString key, QVariant defaultValue){
    return SettingsBackend::getSettingsFor(key, defaultValue);
}

/**
 * @brief Backend::instanceChangedSettings
 * @param instance
 * @param set
 *
 * Reacts to the instance changing its settings(as a set).
 * Saves the new settings.
 * TODO: Needed(overloaded call)?
 */
void Backend::instanceChangedSettings(IInstance *instance, const QHash<QString, QVariant> &set)
{
    SettingsBackend::saveSettingsFor(instance->ID, set);
}

/**
 * @brief Backend::instanceRequestSettings
 * @param instance
 * @param set
 *
 * TODO: Needed(overloaded call)?
 */
void Backend::instanceRequestSettings(IInstance *instance, QHash<QString, QVariant> &set)
{
    set = SettingsBackend::getSettings(instance->ID);
}

/**
 * @brief Backend::runPyFile
 * @param filename
 * @param instructions
 * @param index
 *
 * Creates a thread that executes Python scripts.
 */
void Backend::runPyFile(IInstance *instance){
    auto* thread = new PyLiveThread(instance->ID, this);
    connect(thread, SIGNAL(doneSignal(PyLiveThread*, QString, int)),
            this, SLOT(getExecutionResults(PyLiveThread*, QString, int)));
    thread->initialize(instance->title(), instance->sourceCode());
    thread->start();
    threads.insert(thread->ID, thread);

}

/**
 * @brief Backend::runQtSoundFile
 * @param filename
 * @param instructions
 * @param index
 *
 * Creates a thread that executes QT sound scripts.
 */
void Backend::runQtSoundFile(IInstance *instance){
    auto* thread = new QtSoundThread(instance->ID, this);
    connect(thread, SIGNAL(doneSignal(QtSoundThread*, QString)),
            this, SLOT(getExecutionResults(QtSoundThread*, QString)));
    thread->initialize(instance->title(), instance->sourceCode());
    thread->start();
    threads.insert(thread->ID, thread);
}

/**
 * @brief Backend::runPySoundFile
 * @param filename
 * @param instructions
 * @param index
 *
 * Creates a thread that executes AudioPython scripts.
 */
void Backend::runPySoundFile(IInstance *instance){
    auto* thread = new PySoundThread(instance->ID, this);
    connect(thread, SIGNAL(doneSignal(PySoundThread*, QString, int)),
            this, SLOT(getExecutionResults(PySoundThread*, QString, int)));
    thread->initialize(instance->title(), instance->sourceCode());
    thread->start();
    threads.insert(thread->ID, thread);

}

/**
 * @brief Backend::runGlFile
 * @param filename
 * @param instructions
 * @param index
 *
 * Creates a thread that executes GL source code.
 */
void Backend::runGlFile(IInstance *instance){
    auto* thread = new GlLiveThread(instance->ID, this);
    connect(thread, SIGNAL(doneSignal(GlLiveThread*, QString)),
            this, SLOT(getExecutionResults(GlLiveThread*, QString)));
    connect(thread, SIGNAL(errorSignal(GlLiveThread*, QString, int)),
            this, SLOT(getError(GlLiveThread*, QString, int)));
    thread->initialize(instance->title(), instance->sourceCode());
    thread->start();
    threads.insert(thread->ID, thread);
}

/**
 * @brief Backend::getExecutionResults
 *
 * reacts to the done SIGNAL by terminating the thread and
 * Q_EMITting a showResults SIGNAL for the QWidgets to display
 */
void Backend::getExecutionResults(QtSoundThread* thread, QString returnedException){
    disconnect(thread, SIGNAL(doneSignal(QtSoundThread*, QString)),
            this, SLOT(getExecutionResults(QtSoundThread*, QString)));
    instances[thread->ID]->reportWarning(returnedException);
    terminateThread(thread->ID);
}
void Backend::getExecutionResults(PySoundThread* thread, QString returnedException, int lineno){
    disconnect(thread, SIGNAL(doneSignal(PySoundThread*, QString, int)),
            this, SLOT(getExecutionResults(PySoundThread*, QString, int)));
    instances[thread->ID]->reportWarning(returnedException);
    if(lineno >= 0)
        instances[thread->ID]->highlightErroredLine(lineno);
    terminateThread(thread->ID);
}
void Backend::getExecutionResults(PyLiveThread* thread, QString returnedException, int lineno){
    disconnect(thread, SIGNAL(doneSignal(PyLiveThread*, QString, int)),
            this, SLOT(getExecutionResults(PyLiveThread*, QString, int)));
    instances[thread->ID]->reportWarning(returnedException);
    if(lineno >= 0)
        instances[thread->ID]->highlightErroredLine(lineno);
    terminateThread(thread->ID);
}
void Backend::getExecutionResults(GlLiveThread* thread, QString returnedException){
    // Already gone?
    disconnect(thread, SIGNAL(doneSignal(GlLiveThread*, QString)),
            this, SLOT(getExecutionResults(GlLiveThread*, QString)));
    instances[thread->ID]->reportWarning(returnedException);
    terminateThread(thread->ID);
}

void Backend::getError(GlLiveThread* thread, QString error, int lineno){
    instances[thread->ID]->reportWarning(error);
    if(lineno >= 0)
        instances[thread->ID]->highlightErroredLine(lineno);
}

/**
 * @brief Backend::terminateThread
 * @param thread
 *
 * terminates a specific thread and deletes it from the list.
 */
void Backend::terminateThread(long id){
    if(threads.contains(id)){
        if(threads[id]->isRunning())
            threads[id]->terminate();
        threads[id]->wait();
        threads[id]->deleteLater();
        threads.remove(id);
    }

}

/**
 * @brief Backend::saveIDs
 *
 * saves all the IDs in the settings.
 */
void Backend::saveIDs(){
    QVariantList vids;
    for(const auto i : ids)
        vids.append(i);
    SettingsBackend::addSettings("Instances", vids);
}
