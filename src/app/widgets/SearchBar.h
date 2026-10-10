// SPDX-License-Identifier: GPL-2.0-or-later
// Overgenomen uit Dolphin, Source/Core/DolphinQt/SearchBar.{h,cpp} (GPL-2.0-or-later, commit
// e6f3ae17627e4344da95b13424af5baf4c892b08). Copyright 2018 Dolphin Emulator Project.
#pragma once
#include <QWidget>

class QLineEdit;
class QPushButton;

namespace rg::ui {

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget* parent = nullptr);
    void Show();
    void Hide();
    bool eventFilter(QObject* object, QEvent* event) override;
    void setText(const QString& t);

signals:
    void Search(const QString& serach);

private:
    void CreateWidgets();
    void ConnectWidgets();
    QLineEdit* m_search_edit;
    QPushButton* m_close_button;
};

}  // namespace rg::ui
