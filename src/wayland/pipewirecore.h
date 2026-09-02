// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QHash>

#include <pipewire/pipewire.h>
#include <spa/utils/hook.h>

class QSocketNotifier;
class ScreenCastContext;
class PipeWireStream;
class AbstractPipeWireStream;

class PipeWireCore : public QObject
{
    Q_OBJECT
public:
    PipeWireCore(QObject *parent = nullptr);
    ~PipeWireCore() override;

    bool init();
    bool isValid() const;
    uint64_t objectSerial(uint32_t objectId) const;

    static void onCoreError(void *data,
                            uint32_t id,
                            int seq,
                            int res,
                            const char *message);
    static void onRegistryGlobal(void *data, uint32_t id, uint32_t permissions,
                                 const char *type, uint32_t version,
                                 const struct spa_dict *properties);
    static void onRegistryGlobalRemove(void *data, uint32_t id);

Q_SIGNALS:
    void pipewireFailed(const QString &message);

private:
    friend class ScreenCastContext;
    friend class PipeWireStream;
    friend class AbstractPipeWireStream;

    struct pw_core *m_pwCore = nullptr;
    struct pw_context *m_pwContext = nullptr;
    struct pw_loop *m_pwMainLoop = nullptr;
    spa_hook m_coreListener;
    struct pw_registry *m_registry = nullptr;
    spa_hook m_registryListener;
    QHash<uint32_t, uint64_t> m_objectSerials;
    QString m_error;
    QSocketNotifier *m_notifier = nullptr;

    pw_core_events m_pwCoreEvents = {};
    bool m_valid = false;
};
