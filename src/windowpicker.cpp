// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowpicker.h"
#include "pickerrules.h"
#include <taskmanager/windowtasksmodel.h>
#include <taskmanager/abstracttasksmodel.h>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>

WindowPicker::WindowPicker(Property property,QPlainTextEdit *target,QWidget *parent)
    : QDialog(parent),m_property(property) {
    setObjectName("windowPicker");
    setWindowTitle(tr("Pick an open window"));
    setModal(true);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(580,440);
    auto layout=new QVBoxLayout(this);
    auto search=new QLineEdit(this);
    search->setObjectName("windowPickerSearch");
    search->setPlaceholderText(tr("Search open windows…"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    auto model=new TaskManager::WindowTasksModel(this);
    auto proxy=new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxy->sort(0);
    connect(search,&QLineEdit::textChanged,proxy,&QSortFilterProxyModel::setFilterFixedString);
    auto list=new QListView(this);
    list->setObjectName("openWindows");
    list->setModel(proxy);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list,1);
    m_status=new QLabel(tr("Select a window to choose its rule value."),this);
    m_status->setObjectName("windowPickerStatus");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_values=new QComboBox(this);
    m_values->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_values->setMinimumContentsLength(30);
    m_values->setObjectName("windowRuleValues");
    m_values->setAccessibleName(tr("Rule value"));
    layout->addWidget(m_values);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,this);
    m_add=buttons->addButton(tr("Add rule"),QDialogButtonBox::AcceptRole);
    m_add->setObjectName("addWindowRule");
    m_add->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,this,[this,target] {
        target->setPlainText(Fluid::appendWindowRule(target->toPlainText(),m_values->currentData().toString()));
        accept();
    });
    connect(list->selectionModel(),&QItemSelectionModel::currentChanged,this,[this](const QModelIndex &index) {
        const auto ids=index.data(TaskManager::AbstractTasksModel::WinIdList).toList();
        inspect(ids.isEmpty() ? QString{} : ids.first().toString());
    });
}

void WindowPicker::inspect(const QString &uuid) {
    const int request=++m_request;
    m_values->clear();m_add->setEnabled(false);
    if (uuid.isEmpty()) {m_status->setText(tr("Select an open window."));return;}
    m_status->setText(tr("Reading window details…"));
    auto message=QDBusMessage::createMethodCall("org.kde.KWin","/KWin","org.kde.KWin","getWindowInfo");
    message<<uuid;
    auto watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,request](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<QVariantMap> reply=*watcher;
        watcher->deleteLater();
        if (request!=m_request) return;
        if (reply.isError() || reply.value().isEmpty()) {
            m_status->setText(tr("This window is no longer available. Select another window."));return;
        }
        const auto info=reply.value();
        auto add=[this,&info](const char *key,const QString &label) {
            const auto value=info.value(key).toString();
            if (!Fluid::literalWindowRule(value).isEmpty()) m_values->addItem(label+": "+value,value);
        };
        if (m_property==Application) {
            add("resourceClass",tr("Application ID / class"));
            if (info.value("resourceName")!=info.value("resourceClass")) add("resourceName",tr("Instance"));
        } else if (m_property==Title) add("caption",tr("Window title"));
        else add("role",tr("Window role"));
        m_add->setEnabled(m_values->count()>0);
        m_status->setText(m_values->count() ? tr("Add this value to your existing rules, then Apply to save.")
            : tr("This window does not provide a usable value for this field. Select another window."));
    });
}
