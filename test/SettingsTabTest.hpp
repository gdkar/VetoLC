#ifndef SETTINGSTABTEST
#define SETTINGSTABTEST

#include <QObject>
#include <QTest>
#include "../SettingsTab.hpp"

class SettingsTabTest : public QObject{
Q_OBJECT
private slots:
    void initTestCase() {
        settingsTab = new SettingsTab();
    }
    void objectCreationTest() {
        QVERIFY(settingsTab);
    }
    void cleanupTestCase() {
        delete settingsTab;
    }
private:
    SettingsTab *settingsTab;
};

class LayoutTabTest : public QObject{
Q_OBJECT
public:
    LayoutTabTest(QObject *parent = 0) : QObject(parent){}
private slots:
    void initTestCase() {
        layoutTab = new LayoutTab();
    }
    void objectCreationTest() {
        QVERIFY(layoutTab);
    }
    void cleanupTestCase() {
        delete layoutTab;
    }
private:
    LayoutTab *layoutTab;
};

class BehaviourTabTest : public QObject{
Q_OBJECT
private slots:
    void initTestCase() {
        behaviourTab = new BehaviourTab();
    }
    void objectCreationTest() {
        QVERIFY(behaviourTab);
    }
    void cleanupTestCase() {
        delete behaviourTab;
    }
private:
    BehaviourTab *behaviourTab;
};

#endif // SETTINGSTABTEST
