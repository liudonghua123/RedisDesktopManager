#include "serveritem.h"

#include <asyncfuture.h>

#include <QAction>
#include <QDebug>
#include <QMenu>
#include <QMessageBox>
#include <algorithm>
#include <functional>

#include "connections-tree/model.h"
#include "connections-tree/utils.h"
#include "databaseitem.h"

using namespace ConnectionsTree;

ServerItem::ServerItem(QSharedPointer<Operations> operations, Model& model,
                       QWeakPointer<TreeItem> parent)
    : SortableTreeItem(model),
      m_operations(operations),
      m_parent(parent) {}

ServerItem::~ServerItem() {}

QString ServerItem::getDisplayName() const { return m_operations->connectionName(); }

QList<QSharedPointer<TreeItem> > ServerItem::getAllChilds() const {
  return m_databases;
}

uint ServerItem::childCount(bool) const {
  return static_cast<uint>(m_databases.size());
}

QSharedPointer<TreeItem> ServerItem::child(uint row) {
  if (row < m_databases.size()) {
    return m_databases.at(row);
  }

  return QSharedPointer<TreeItem>();
}

QWeakPointer<TreeItem> ServerItem::parent() const {
    return m_parent;
}

void ServerItem::setParent(QWeakPointer<TreeItem> p)
{
    m_parent = p;
}

bool ServerItem::isDatabaseListLoaded() const {
  return m_databases.size() > 0;
}

QSharedPointer<Operations> ServerItem::getOperations() { return m_operations; }

int ServerItem::row() const
{
    if (!parent()) {
        return m_row;
    }

    return TreeItem::row();
}

void ServerItem::load() {
  auto callback = QSharedPointer<Operations::GetDatabasesCallback>(
      new Operations::GetDatabasesCallback(
          getSelf(),
          [this](RedisClient::DatabaseList databases, const QString& err) {
            if (err.size() > 0) {
              unlock();
              emit m_model.error(QCoreApplication::translate(
                                     "RESP", "Cannot load databases:\n\n") +
                                 err);
              return;
            }

            if (databases.size() == 0) {
              unlock();
              return;
            }

            RedisClient::DatabaseList::const_iterator db =
                databases.constBegin();
            QList<QSharedPointer<TreeItem>> dbs;
            while (db != databases.constEnd()) {
              QSharedPointer<TreeItem> database((new DatabaseItem(
                  db.key(), db.value(), m_operations, m_self, m_model)));

              dbs.push_back(database);
              ++db;
            }

            QTimer::singleShot(0, this, [this, dbs]() {
              m_model.beforeChildLoaded(getSelf(), dbs.size());
              m_databases = dbs;
              m_model.childLoaded(getSelf());

              unlock();
              m_model.expandItem(getSelf());
            });
          }));

  m_currentOperation = m_operations->getDatabases(callback);

  if (!m_currentOperation.isRunning()) {
    unlock();
  }
}

void ServerItem::unload() {
  if (!isDatabaseListLoaded()) return;

  m_model.beforeItemChildsUnloaded(m_self);

  m_operations->disconnect();

  for (auto db : m_databases) {
      auto dbItem = db.staticCast<DatabaseItem>();

      if (dbItem && m_operations) {
          m_operations->notifyDbWasUnloaded(dbItem->getDbIndex());
      }
  }

  m_databases.clear();
  m_model.itemChildRemoved(getSelf());  
}

void ServerItem::reload() {
  unload();
  load();
}

void ServerItem::edit() {
  unload();
  emit editActionRequested();
}

void ServerItem::remove() {
  unload();
  emit deleteActionRequested();
}

void ServerItem::openConsole() { m_operations->openConsoleTab(); }

QHash<QString, std::function<bool()> > ServerItem::eventHandlers() {
  auto events = TreeItem::eventHandlers();

  events.insert("click", [this]() {
    m_operations->openServerStats();

    if (isDatabaseListLoaded()) {
        if (!isExpanded()) {
            setExpanded(true);
            m_model.expandItem(getSelf());
        }
        return true;
    }

    load();    
    return false;
  });

  events.insert("console", [this]() { m_operations->openConsoleTab(); return true; });

  auto openServerContextTab = [this]() { m_operations->openServerStats(); return true; };
  events.insert("right-click", openServerContextTab);
  events.insert("mid-click", openServerContextTab);

  events.insert("duplicate", [this]() { m_operations->duplicateConnection(); return true; });

  events.insert("reload", [this]() {
    reload();
    return false;
  });

  events.insert("unload", [this]() { unload(); return true; });

  events.insert("edit", [this]() {
    auto unloadAction = [this]() {
      unload();
      emit editActionRequested();
    };

    if (m_operations->isConnected()) {
      confirmAction(nullptr,
                    QCoreApplication::translate(
                        "RESP",
                        "Value and Console tabs related to this "
                        "connection will be closed. Do you want to continue?"),
                    unloadAction);
    } else {
      unloadAction();
    }
    return true;
  });

  events.insert("delete", [this]() {
    confirmAction(nullptr,
                  QCoreApplication::translate(
                      "RESP", "Do you really want to delete connection?"),
                  [this]() {
                    unload();
                    emit deleteActionRequested();
                  });
    return true;
  });

  return events;
}

void ServerItem::setWeakPointer(QWeakPointer<ServerItem> self) {
  m_self = self;
  m_selfPtr = self;
}

QVariantMap ConnectionsTree::ServerItem::metadata() const {
  QVariantMap meta = TreeItem::metadata();

  if (isDatabaseListLoaded()) {
    meta["server_type"] = m_operations->mode();
  } else {
    meta["server_type"] = "unknown";
  }

  meta["user_color"] = m_operations->iconColor();

  return meta;
}
