#include "PySoundGenerator.hpp"

#if PY_MAJOR_VERSION >= 3
/**
 * @brief PySoundGenerator::PySoundGenerator
 * @param progName
 * @param pyInstructions
 *
 * The constructor of the PySoundGeneratorclass.
 * Sets up the python interpreter, the instructions with
 * which it will be fed, the class variables and the break shortcut.
 */
PySoundGenerator::PySoundGenerator(char* progName, char* pyInstructions){
    if(pyInstructions == QString()){
        Q_EMIT doneSignal(tr("File is empty. Nothing to execute."), -1);
        return;
    }

    ownExcept = QString();
    exceptNum = -1;
    abortAction = new QAction(this);
    abortAction->setShortcut(QKeySequence("Ctrl-C"));
    connect(abortAction, SIGNAL(triggered()), this, SLOT(terminated()));

    setupPython(progName, pyInstructions);

    device = new AudioOutputProcessor();
    connect(device, SIGNAL(startWriting()), this, SLOT(setReady()));
    ready = true;
    device->start();
}

/**
 * @brief PySoundGenerator::~PySoundGenerator
 *
 * The destructor of the PySoundGenerator class.
 * Kills the python interpreter.
 */
PySoundGenerator::~PySoundGenerator(){
    device->exit();
    delete abortAction;
    if(Py_IsInitialized())
        Py_Finalize();
    // Wait for the end of QThread before deletion, otherwise it raises an error
    while(device->isRunning())
        ;
    delete device;
}

/**
 * @brief PySoundGenerator::setupPython
 * @param instructions
 *
 * sets up the Python interpreter.
 */
void PySoundGenerator::setupPython(QString instructions){
    Py_Initialize();
    Py_SetProgramName(progName);
    main = PyImport_AddModule("__main__");

    PyModule_AddStringConstant(main, "__file__", "");

    dict = PyModule_GetDict(main);

    if(!dict){
        exceptionOccurred();
        Q_EMIT doneSignal(ownExcept, -1);
        return;
    }



    for(QString instructionSet : toExecute){
        check = execute(instructionSet);
        if(!check){
            exceptionOccurred();
            break;
        }
    }
}

/**
 * @brief PySoundGenerator::run
 *
 * The main loop. Calls the user code executing method
 * and Q_EMITs a signal when it's done.
 */
void PySoundGenerator::run(){
    if(ownExcept != ""){
        Q_EMIT doneSignal(ownExcept, exceptNum);
        return;
    }
    write();
    while(!ready)
        QCoreApplication::processEvents();
}

/**
 * @brief PySoundGenerator::write
 *
 * Gets samples and does error handling. The actual
 * main loop, so to say.
 */
void PySoundGenerator::write(){
    while(ready){
        PyObject* check = execute("AudioPython.yield_raw(samples, None)");
        if(!check){
            exceptionOccurred();
            Q_EMIT doneSignal(ownExcept, exceptNum);
            break;
        }
        if(PyBytes_Check(check))
            stream(check);
        Py_XDECREF(check);
    }
}

/**
 * @brief PySoundGenerator::execute_return
 * @return PyObject* / NULL if there was an exception
 *         in the python interpreter.
 *
 * executes the python code in the interpreter and returns the result.
 */
PyObject* PySoundGenerator::execute_return(QString mod, QString function, QString args){
    PyObject* pyModule = PyObject_GetAttrString(main, mod.toLocal8Bit().data());
    if(!pyModule) return 0;
    PyObject* pyFunction = PyObject_GetAttrString(pyModule, function.toLocal8Bit().data());
    if(!pyFunction) return 0;
    if(args.isEmpty()){
        return PyObject_CallObject(pyFunction, NULL);
    }
    PyObject* arguments = PyTuple_Pack(1, args.toLocal8Bit().data());
    return PyObject_CallObject(pyFunction, arguments);
}

/**
 * @brief PySoundGenerator::execute
 * @return PyObject* / NULL if there was an exception
 *         in the python interpreter.
 *
 * executes the python code in the interpreter.
 */
PyObject* PySoundGenerator::execute(QString instruct){
    return PyRun_String(instruct.toLocal8Bit().data(),
                        Py_file_input, dict, dict);
}

/**
 * @brief PySoundGenerator::stream
 * @param process
 *
 * writes the generated bytes to the IODevice
 */
void PySoundGenerator::stream(PyObject* data){
    ready = device->write(PyBytes_AsString(data), PyBytes_Size(data));
}

/**
 * @brief PySoundGenerator::updateCode
 * @param filename
 * @param instructions
 * @return true if the name of the program could be changed,
 *         false otherwise
 *
 * Updates the code of the currently running Python interpreter.
 */
bool PySoundGenerator::updateCode(QString filename, QString instructions){
    setupPython(filename, instructions);
    return true;
}

/**
 * @brief PySoundGenerator::exceptionOccurred
 * @return PythonException
 *
 * Fetches the Python Exception and translates it to a QString
 * and an int representing exception and line number.
 */
void PySoundGenerator::exceptionOccurred(){
    PyObject *errtype, *errvalue, *traceback;
    PyObject *mod;
    PyObject *ret, *list, *string;
    PyErr_Fetch(&errtype, &errvalue, &traceback);
    PyErr_NormalizeException(&errtype, &errvalue, &traceback);
    QString exceptionText = QString(PyBytes_AsString(PyObject_Str(errtype)));
    exceptionText.append(": '");
    exceptionText.append(PyBytes_AsString(PyObject_Str(errvalue)));
    QRegExp line("line [0-9]+");
    if(line.indexIn(exceptionText) != -1){
        QString text = line.capturedTexts().at(0);
        exceptionText.replace(" (<string>, " + text + ")", "");
        exceptionText.append("' at " + text);
        text.replace("line ", "");
        exceptNum = text.toInt();
    } else{
        mod = PyImport_ImportModule("traceback");
        list = PyObject_CallMethod(mod, (char*)"format_exception",
                               (char*)"OOO", errtype, errvalue, traceback);
        if(list == 0){
            ownExcept = exceptionText;
            exceptNum = -1;
            return;
        }
        string = PyBytes_FromString("\n");
        ret = _PyBytes_Join(string, list);
        if(line.indexIn(PyBytes_AsString(ret)) != -1){
            QString text = line.capturedTexts().at(0);
            exceptionText.append("' at " + text);
            text.replace("line ", "");
            exceptNum = text.toInt();
        } else{
            exceptNum = -1;
        }
        Py_XDECREF(list);
        Py_XDECREF(string);
        Py_XDECREF(ret);
    }
    PyErr_Clear();
    ownExcept = exceptionText;
}

/**
 * @brief PySoundGenerator::terminated
 *
 * SLOT that is called when the user interrupt(CTRL-C) SIGNAL
 * is Q_EMITted.
 */
void PySoundGenerator::terminated(){
    ownExcept = tr("User Terminated.");
}

/**
 * @brief PySoundGenerator::setReady
 * @param set
 *
 * Setter for the ready-variable that indicates
 * whether writing to the IODevice is allowed.
 */
void PySoundGenerator::setReady(){
    ready = true;
    write();
}
#else
/**
 * @brief PySoundGenerator::PySoundGenerator
 * @param progName
 * @param pyInstructions
 *
 * The constructor of the PySoundGenerator class.
 * Sets up the python interpreter, the instructions with
 * which it will be fed, the class variables and the break shortcut.
 */
PySoundGenerator::PySoundGenerator(char* progName, char* pyInstructions){
    if(pyInstructions == QString()){
        Q_EMIT doneSignal(tr("File is empty. Nothing to execute."), -1);
        return;
    }

    ownExcept = QString();
    exceptNum = -1;
    abortAction = new QAction(this);
    abortAction->setShortcut(QKeySequence("Ctrl-C"));
    connect(abortAction, SIGNAL(triggered()), this, SLOT(terminated()));

    setupPython(progName, pyInstructions);

    device = new AudioOutputProcessor();
    connect(device, SIGNAL(startWriting()), this, SLOT(setReady()));
    ready = true;
    device->start();
}

/**
 * @brief PySoundGenerator::~PySoundGenerator
 *
 * The destructor of the PySoundGenerator class.
 * Kills the python interpreter.
 */
PySoundGenerator::~PySoundGenerator(){
    device->exit();
    delete abortAction;
    if(Py_IsInitialized())
        Py_Finalize();
    // Wait for the end of QThread before deletion, otherwise it raises an error
    while(device->isRunning())
        ;
    delete device;
}

/**
 * @brief PySoundGenerator::setupPython
 * @param instructions
 *
 * sets up the Python interpreter.
 */
void PySoundGenerator::setupPython(QString progName, QString instructions){
    Py_Initialize();
    Py_SetProgramName(progName.toLocal8Bit().data());
    main = PyImport_AddModule("__main__");

    PyModule_AddStringConstant(main, "__file__", "");

    dict = PyModule_GetDict(main);

    if(!dict){
        exceptionOccurred();
        Q_EMIT doneSignal(ownExcept, -1);
        return;
    }

    QList<QString> toExecute;
    toExecute.append("import AudioPython");
    toExecute.append("from AudioPython import *");
    toExecute.append(instructions);
    toExecute.append("samples = AudioPython.compute_samples(channels, None)");
    toExecute.append("gen = AudioPython.yield_raw(samples, None)");

    PyObject* check;

    for(QString instructionSet : toExecute){
        check = execute(instructionSet);
        if(!check){
            exceptionOccurred();
            break;
        }
    }
}

/**
 * @brief PySoundGenerator::run
 *
 * The main loop. Calls the user code executing method
 * and Q_EMITs a signal when it's done.
 */
void PySoundGenerator::run(){ 
    if(!ownExcept.isEmpty()){
        Q_EMIT doneSignal(ownExcept, exceptNum);
        return;
    }
    write();
    while(!ready)
        QCoreApplication::processEvents();
}

/**
 * @brief PySoundGenerator::write
 *
 * Gets samples and does error handling. The actual
 * main loop, so to say.
 */
void PySoundGenerator::write(){
    while(ready){
        PyObject* check = execute_return("gen", "next", "");
        if(!check){
            exceptionOccurred();
            Q_EMIT doneSignal(ownExcept, exceptNum);
            break;
        }
        if(PyString_Check(check))
            stream(check);
        Py_XDECREF(check);
    }
    qDebug() << "Jump out";
}

/**
 * @brief PySoundGenerator::execute_return
 * @return PyObject* / NULL if there was an exception
 *         in the python interpreter.
 *
 * executes the python code in the interpreter and returns the result.
 */
PyObject* PySoundGenerator::execute_return(QString mod, QString function, QString args){
    PyObject* pyModule = PyObject_GetAttrString(main, mod.toLocal8Bit().data());
    if(!pyModule) return 0;
    PyObject* pyFunction = PyObject_GetAttrString(pyModule, function.toLocal8Bit().data());
    if(!pyFunction) return 0;
    if(args.isEmpty()){
        return PyObject_CallObject(pyFunction, NULL);
    }
    PyObject* arguments = PyTuple_Pack(1, args.toLocal8Bit().data());
    return PyObject_CallObject(pyFunction, arguments);
}

/**
 * @brief PySoundGenerator::execute
 * @return PyObject* / NULL if there was an exception
 *         in the python interpreter.
 *
 * executes the python code in the interpreter.
 */
PyObject* PySoundGenerator::execute(QString instruct){
    return PyRun_String(instruct.toLocal8Bit().data(),
                        Py_file_input, dict, dict);
}

/**
 * @brief PySoundGenerator::stream
 * @param process
 *
 * writes the generated bytes to the IODevice
 */
void PySoundGenerator::stream(PyObject* data){
    ready = device->write(PyBytes_AsString(data), PyBytes_Size(data));
    qDebug() << ready;
}

/**
 * @brief PySoundGenerator::updateCode
 * @param filename
 * @param instructions
 * @return true if the name of the program could be changed,
 *         false otherwise
 *
 * Updates the code of the currently running Python interpreter.
 */
bool PySoundGenerator::updateCode(QString filename, QString instructions){
    setupPython(filename, instructions);
    return true;
}

/**
 * @brief PySoundGenerator::exceptionOccurred
 * @return PythonException
 *
 * Fetches the Python Exception and translates it to a QString
 * and an int representing exception and line number.
 */
void PySoundGenerator::exceptionOccurred(){
    PyObject *errtype, *errvalue, *traceback;
    PyObject *mod;
    PyObject *ret, *list, *string;
    PyErr_Fetch(&errtype, &errvalue, &traceback);
    PyErr_NormalizeException(&errtype, &errvalue, &traceback);
    QString exceptionText = QString(PyString_AsString(PyObject_Str(errtype)));
    exceptionText.append(": '");
    exceptionText.append(PyString_AsString(PyObject_Str(errvalue)));
    QRegExp line("line [0-9]+");
    if(line.indexIn(exceptionText) != -1){
        QString text = line.capturedTexts().at(0);
        exceptionText.replace(" (<string>, " + text + ")", "");
        exceptionText.append("' at " + text);
        text.replace("line ", "");
        exceptNum = text.toInt();
    } else{
        mod = PyImport_ImportModule("traceback");
        list = PyObject_CallMethod(mod, (char*)"format_exception",
                               (char*)"OOO", errtype, errvalue, traceback);
        if(list == 0){
            ownExcept = exceptionText;
            exceptNum = -1;
            return;
        }
        string = PyString_FromString("\n");
        ret = _PyString_Join(string, list);
        if(line.indexIn(PyString_AsString(ret)) != -1){
            QString text = line.capturedTexts().at(0);
            exceptionText.append("' at " + text);
            text.replace("line ", "");
            exceptNum = text.toInt();
        } else{
            exceptNum = -1;
        }
        Py_DECREF(list);
        Py_DECREF(string);
        Py_DECREF(ret);
    }
    PyErr_Clear();
    ownExcept = exceptionText;
}

/**
 * @brief PySoundGenerator::terminated
 *
 * SLOT that is called when the user interrupt(CTRL-C) SIGNAL
 * is Q_EMITted.
 */
void PySoundGenerator::terminated(){
    ownExcept = tr("User Terminated.");
}

/**
 * @brief PySoundGenerator::setReady
 * @param set
 *
 * Setter for the ready-variable that indicates
 * whether writing to the IODevice is allowed.
 */
void PySoundGenerator::setReady(){
    ready = true;
    write();
}
#endif
