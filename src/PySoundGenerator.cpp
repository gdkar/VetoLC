#include "PySoundGenerator.hpp"

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
        emit doneSignal(tr("File is empty. Nothing to execute."), -1);
        return;
    }

    Py_SetProgramName(progName);
    Py_Initialize();
    ownExcept = QString();
    exceptNum = -1;
    abortAction = new QAction(this);
    abortAction->setShortcut(QKeySequence("Ctrl-C"));
    connect(abortAction, SIGNAL(triggered()), this, SLOT(terminated()));

    PyObject* module = PyImport_AddModule("__main__");
    sys = PyImport_ImportModule("sys");
//    PyObject *path = PyObject_GetAttrString(sys, "path");
//    PyList_Append(path, PyString_FromString("../../../"));
//    PyList_Append(path, PyString_FromString("."));
    dict = PyModule_GetDict(module);
    if(!dict){
        exceptionOccurred();
        emit doneSignal(ownExcept, -1);
        return;
    }
    execute("import AudioPython");
    execute(pyInstructions);
    execute("samples = AudioPython.compute_samples(channels, None)");



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
    Py_DECREF(sys);
    delete abortAction;
    Py_Finalize();
    device->exit();
    // Wait for the end of QThread before deletion, otherwise it raises an error
    while(device->isRunning())
        ;
    delete device;
}

/**
 * @brief PySoundGenerator::run
 *
 * The main loop. Calls the user code executing method
 * and emits a signal when it's done.
 */
void PySoundGenerator::run(){
    write();
    while(!ready)
        QCoreApplication::processEvents();
}

void PySoundGenerator::write(){
    while(ready){
        PyObject* check = execute("AudioPython.yield_raw(samples, None)");
        if(!check){
            exceptionOccurred();
            emit doneSignal(ownExcept, exceptNum);
            break;
        }
        if(PyBytes_Check(check))
            stream(check);
    }
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
    Py_SetProgramName(filename.toLocal8Bit().data());
    execute(instructions.toLocal8Bit().data());
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
 * is emitted.
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
