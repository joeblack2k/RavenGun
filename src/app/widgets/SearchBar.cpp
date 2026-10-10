// SPDX-License-Identifier: GPL-2.0-or-later
// Overgenomen uit Dolphin, Source/Core/DolphinQt/SearchBar.cpp (GPL-2.0-or-later, commit
// e6f3ae17627e4344da95b13424af5baf4c892b08). Copyright 2018 Dolphin Emulator Project.
#include "widgets/SearchBar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>

namespace rg::ui {

SearchBar::SearchBar(QWidget* parent) : QWidget(parent) {
    CreateWidgets();
    ConnectWidgets();
    setFixedHeight(40);
    setHidden(true);
    installEventFilter(this);
}

void SearchBar::CreateWidgets() {
    m_search_edit = new QLineEdit;
    m_close_button = new QPushButton(tr("Close"));
    m_search_edit->setPlaceholderText(tr("Search games..."));
    m_search_edit->setClearButtonEnabled(true);
    auto* layout = new QHBoxLayout;
    layout->addWidget(m_search_edit);
    layout->addWidget(m_close_button);
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    setLayout(layout);
}

void SearchBar::Show() {
    m_search_edit->setFocus();
    m_search_edit->selectAll();
    // Re-apply the filter string.
    emit Search(m_search_edit->text());
    show();
}

void SearchBar::Hide() {
    // Clear the filter string.
    emit Search(QString());
    m_search_edit->clearFocus();
    hide();
}

void SearchBar::setText(const QString& t) { m_search_edit->setText(t); }

void SearchBar::ConnectWidgets() {
    connect(m_search_edit, &QLineEdit::textChanged, this, &SearchBar::Search);
    connect(m_close_button, &QPushButton::clicked, this, &SearchBar::Hide);
}

bool SearchBar::eventFilter(QObject*, QEvent* event) {
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) Hide();
    return false;
}

}  // namespace rg::ui
