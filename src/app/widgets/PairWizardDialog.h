// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Koppelwizard voor een PS Move of Navigation Controller: dezelfde toestandsmachine als de webapp (IPC `pair` start,
// status, confirm, cancel; docs/architecture.md, "Koppelen en verwijderen"), met Engelse teksten per stap.
#include <QDialog>
#include <QJsonObject>

#include "core/AppCore.h"

class QLabel;
class QPushButton;
class QTimer;
class QLineEdit;

namespace rg::ui {

class PairWizardDialog final : public QDialog {
    Q_OBJECT
public:
    PairWizardDialog(rg::AppCore* core, QWidget* parent = nullptr);
    static QString StepText(const QString& state, const QString& device);
    static int StepNumber(const QString& state);  // 1..4, 0 = geen

private:
    void Start();
    void Poll();
    void Show(const QJsonObject& pairing);
    void reject() override;

    rg::AppCore* core_;
    QLabel* m_steps = nullptr;
    QLabel* m_text = nullptr;
    QLabel* m_detail = nullptr;
    QLineEdit* m_command = nullptr;
    QPushButton* m_start = nullptr;
    QPushButton* m_confirm = nullptr;
    QPushButton* m_cancel = nullptr;
    QTimer* m_poll = nullptr;
    QString m_state = QStringLiteral("idle");
};

}  // namespace rg::ui
