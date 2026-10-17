// SPDX-License-Identifier: GPL-2.0-or-later
// Naar Dolphin, Source/Core/DolphinQt/GameList/GameList.cpp, ListProxyModel.cpp en GridProxyModel.cpp
// (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08). Copyright 2015/2017 Dolphin Emulator Project.
#include "widgets/GameList.h"

#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QShortcut>
#include <QTableView>
#include <cmath>
#include <vector>

#include "core/AppPaths.h"
#include "widgets/Resources.h"

namespace rg::ui {

using Column = rg::GameListModel::Column;

namespace {

QSettings& settings() {
    static QSettings s(rg::AppPaths::appSettingsFile(), QSettings::IniFormat);
    return s;
}

class GameListTableView : public QTableView {
public:
    explicit GameListTableView(QWidget* parent = nullptr) : QTableView(parent) {}

protected:
    QModelIndex moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers) override {
        // Columns are irrelevant for the game list, so treat the home/end press as if control were held.
        if (cursorAction == CursorAction::MoveHome || cursorAction == CursorAction::MoveEnd)
            return QTableView::moveCursor(cursorAction, modifiers | Qt::ControlModifier);
        return QTableView::moveCursor(cursorAction, modifiers);
    }
    void mouseDoubleClickEvent(QMouseEvent* const event) override {
        if (event->button() == Qt::LeftButton) QTableView::mouseDoubleClickEvent(event);
    }
};

class GameListListView : public QListView {
public:
    explicit GameListListView(QWidget* parent = nullptr) : QListView(parent) {}

protected:
    void mouseDoubleClickEvent(QMouseEvent* const event) override {
        if (event->button() == Qt::LeftButton) QListView::mouseDoubleClickEvent(event);
    }
};

// Dubbelklik op de lege weergave (Dolphin: DoubleClickEventFilter).
class DoubleClickEventFilter : public QObject {
public:
    explicit DoubleClickEventFilter(std::function<void()> f, QObject* parent) : QObject(parent), f_(std::move(f)) {}
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::MouseButtonDblClick) f_();
        return false;
    }

private:
    std::function<void()> f_;
};

bool lessByTitle(const QSortFilterProxyModel* p, const QModelIndex& left, const QModelIndex& right) {
    const auto* src = p->sourceModel();
    const auto lt = src->index(left.row(), static_cast<int>(Column::Title)).data(rg::GameListModel::SORT_ROLE).toString();
    const auto rt = src->index(right.row(), static_cast<int>(Column::Title)).data(rg::GameListModel::SORT_ROLE).toString();
    if (p->sortOrder() == Qt::AscendingOrder) return lt.compare(rt, Qt::CaseInsensitive) < 0;
    return rt.compare(lt, Qt::CaseInsensitive) < 0;
}

}  // namespace

// --- ListProxyModel (Dolphin ListProxyModel.cpp) ---------------------------------------------------------------------

ListProxyModel::ListProxyModel(QObject* parent) : QSortFilterProxyModel(parent) { setDynamicSortFilter(true); }

bool ListProxyModel::filterAcceptsRow(int source_row, const QModelIndex&) const {
    auto* glm = qobject_cast<rg::GameListModel*>(sourceModel());
    return glm->ShouldDisplayGameListItem(source_row);
}

bool ListProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    if (left.data(rg::GameListModel::SORT_ROLE) != right.data(rg::GameListModel::SORT_ROLE))
        return QSortFilterProxyModel::lessThan(left, right);
    // If two items are otherwise equal, compare them by their title
    return lessByTitle(this, left, right);
}

// --- GridProxyModel (Dolphin GridProxyModel.cpp) ---------------------------------------------------------------------

GridProxyModel::GridProxyModel(QObject* parent) : QSortFilterProxyModel(parent) { setDynamicSortFilter(true); }

QVariant GridProxyModel::data(const QModelIndex& i, int role) const {
    const QModelIndex source_index = mapToSource(i);
    auto* model = static_cast<rg::GameListModel*>(sourceModel());
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
        return model->index(source_index.row(), static_cast<int>(Column::Title)).data(Qt::DisplayRole);
    if (role == Qt::DecorationRole) return model->index(source_index.row(), 0).data(rg::GameListModel::LARGE_ICON_ROLE);
    if (role == Qt::FontRole) return model->index(source_index.row(), static_cast<int>(Column::Title)).data(Qt::FontRole);
    if (role == rg::GameListModel::ID_ROLE) return model->index(source_index.row(), 0).data(rg::GameListModel::ID_ROLE);
    return {};
}

bool GridProxyModel::filterAcceptsRow(int source_row, const QModelIndex&) const {
    auto* glm = qobject_cast<rg::GameListModel*>(sourceModel());
    return glm->ShouldDisplayGameListItem(source_row);
}

bool GridProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    if (left.data(rg::GameListModel::SORT_ROLE) != right.data(rg::GameListModel::SORT_ROLE))
        return QSortFilterProxyModel::lessThan(left, right);
    return lessByTitle(this, left, right);
}

// --- GameList (Dolphin GameList.cpp) ---------------------------------------------------------------------------------

GameList::GameList(rg::GameListModel* model, QWidget* parent) : QStackedWidget(parent), m_model(model) {
    m_list_proxy = new ListProxyModel(this);
    m_list_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_list_proxy->setSortRole(rg::GameListModel::SORT_ROLE);
    m_list_proxy->setSourceModel(m_model);
    m_grid_proxy = new GridProxyModel(this);
    m_grid_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_grid_proxy->setSortRole(rg::GameListModel::SORT_ROLE);
    m_grid_proxy->setSourceModel(m_model);

    if (settings().contains(QStringLiteral("gridview/scale"))) m_model->setScale(settings().value(QStringLiteral("gridview/scale")).toDouble());

    MakeListView();
    MakeGridView();
    MakeEmptyView();

    // Use List View's sorting for Grid View too.
    m_grid_proxy->sort(m_list_proxy->sortColumn(), m_list_proxy->sortOrder());
    connect(m_list->horizontalHeader(), &QHeaderView::sortIndicatorChanged, m_grid_proxy, [this](int c, Qt::SortOrder o) { m_grid_proxy->sort(c, o); });

    connect(m_list, &QTableView::doubleClicked, this, &GameList::GameSelected);
    connect(m_grid, &QListView::doubleClicked, this, &GameList::GameSelected);
    connect(m_model, &QAbstractItemModel::modelReset, this, &GameList::ConsiderViewChange);
    connect(m_model, &QAbstractItemModel::modelReset, this, &GameList::UpdateGameCount);

    addWidget(m_list);
    addWidget(m_grid);
    addWidget(m_empty);
    m_prefer_list = settings().value(QStringLiteral("gamelist/prefer_list"), true).toBool();
    ConsiderViewChange();
    UpdateFont();

    auto* zoom_in = new QShortcut(QKeySequence::ZoomIn, this);
    auto* zoom_out = new QShortcut(QKeySequence::ZoomOut, this);
    zoom_in->setContext(Qt::WidgetWithChildrenShortcut);
    zoom_out->setContext(Qt::WidgetWithChildrenShortcut);
    connect(zoom_in, &QShortcut::activated, this, &GameList::ZoomIn);
    connect(zoom_out, &QShortcut::activated, this, &GameList::ZoomOut);
    const auto* zoom_in_alternate = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this);
    connect(zoom_in_alternate, &QShortcut::activated, this, &GameList::ZoomIn);
}

GameList::~GameList() {
    settings().setValue(QStringLiteral("tableheader/state"), m_list->horizontalHeader()->saveState());
    settings().setValue(QStringLiteral("gridview/scale"), m_model->scale());
}

void GameList::MakeListView() {
    m_list = new GameListTableView(this);
    m_list->setModel(m_list_proxy);
    m_list->setTabKeyNavigation(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setAlternatingRowColors(true);
    m_list->setShowGrid(false);
    m_list->setSortingEnabled(true);
    m_list->setCurrentIndex(QModelIndex());
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setWordWrap(false);
    m_list->setIconSize(QSize(32, 32));
    // Have 1 pixel of padding above and below the 32 pixel icons.
    m_list->verticalHeader()->setDefaultSectionSize(32 + 6);

    QHeaderView* hor_header = m_list->horizontalHeader();
    const bool restored = hor_header->restoreState(settings().value(QStringLiteral("tableheader/state")).toByteArray());
    if (!restored) m_list->sortByColumn(static_cast<int>(Column::Title), Qt::AscendingOrder);

    using Mode = QHeaderView::ResizeMode;
    hor_header->setSectionResizeMode(static_cast<int>(Column::Icon), Mode::Fixed);
    for (Column c : {Column::Title, Column::Platform, Column::Status, Column::Route, Column::Notes, Column::Profile, Column::Path})
        hor_header->setSectionResizeMode(static_cast<int>(c), Mode::Interactive);
    m_list->setColumnWidth(static_cast<int>(Column::Icon), 44);
    if (!restored) {
        m_list->setColumnWidth(static_cast<int>(Column::Title), 260);
        m_list->setColumnWidth(static_cast<int>(Column::Platform), 170);
        m_list->setColumnWidth(static_cast<int>(Column::Status), 120);
        m_list->setColumnWidth(static_cast<int>(Column::Route), 220);
        m_list->setColumnWidth(static_cast<int>(Column::Notes), 300);
        m_list->setColumnHidden(static_cast<int>(Column::Profile), true);
        m_list->setColumnHidden(static_cast<int>(Column::Path), true);
    }
    hor_header->setMinimumSectionSize(38);
    m_list->verticalHeader()->hide();
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFrameStyle(QFrame::NoFrame);
    hor_header->setSectionsMovable(true);
    hor_header->setHighlightSections(false);
    connect(hor_header, &QHeaderView::sectionResized, this, [this] { OnHeaderViewChanged(); });

    connect(m_list, &QTableView::customContextMenuRequested, this, &GameList::ShowContextMenu);
    connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection&, const QItemSelection&) { emit SelectionChanged(GetSelectedGameId()); });
}

void GameList::MakeGridView() {
    m_grid = new GameListListView(this);
    m_grid->setModel(m_grid_proxy);
    m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setWordWrap(true);
    m_grid->setSpacing(10);
    m_grid->setUniformItemSizes(true);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    m_grid->setFrameStyle(QFrame::NoFrame);
    m_grid->setCurrentIndex(m_grid->indexAt(QPoint(0, 0)));
    m_grid->clearSelection();
    connect(m_grid, &QListView::customContextMenuRequested, this, &GameList::ShowContextMenu);
    connect(m_grid->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection&, const QItemSelection&) { emit SelectionChanged(GetSelectedGameId()); });
}

void GameList::MakeEmptyView() {
    m_empty = new QLabel(this);
    m_empty->setText(tr("RavenGun could not find any games.\nDouble-click here to add a folder with games..."));
    m_empty->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_empty->setEnabled(true);
    m_empty->installEventFilter(new DoubleClickEventFilter([this] { emit AddFolderRequested(); }, m_empty));
    QSizePolicy size_policy{m_empty->sizePolicy()};
    size_policy.setRetainSizeWhenHidden(true);
    m_empty->setSizePolicy(size_policy);
}

void GameList::SetRefreshing(bool refreshing) {
    if (refreshing && m_model->rowCount() == 0) {
        m_empty->setText(tr("Refreshing..."));
        m_empty->setEnabled(false);
    } else {
        m_empty->setText(tr("RavenGun could not find any games.\nDouble-click here to add a folder with games..."));
        m_empty->setEnabled(true);
    }
}

void GameList::resizeEvent(QResizeEvent*) { OnHeaderViewChanged(); }

void GameList::ShowContextMenu(const QPoint&) {
    const QString id = GetSelectedGameId();
    if (id.isEmpty()) return;
    const rg::GameEntry* g = m_model->game(m_model->rowOf(id));
    if (!g) return;
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose, true);
    QAction* play = menu->addAction(Resources::GetThemeIcon("play"), tr("&Play"), this, &GameList::GameSelected);
    play->setEnabled(g->kind == rg::GameKind::TeknoParrot || g->kind == rg::GameKind::Unity);
    menu->addAction(Resources::GetThemeIcon("properties"), tr("P&roperties / Edit..."), this, &GameList::OpenProperties);
    menu->addSeparator();
    if (g->kind == rg::GameKind::Detected) {
        menu->addAction(tr("&Set Up Game..."), this, &GameList::CreateProfile);
        menu->addSeparator();
    } else if (g->kind == rg::GameKind::Unconfigured) {
        menu->addAction(tr("&Link to Known Game..."), this, &GameList::OpenProperties);
        menu->addSeparator();
    }
    menu->addAction(tr("Set as &Default Game"), this, &GameList::SetDefaultGame);
    // Een eigen snelkoppeling in Steam (Game Mode): het startscherm (PS Move aan, kalibratie) en dan de game.
    QAction* steam = menu->addAction(tr("Add to &Steam..."), this, &GameList::AddToSteam);
    steam->setEnabled(g->hasProfile());
    menu->addAction(Resources::GetThemeIcon("folder"), tr("Open &Containing Folder"), this, &GameList::OpenContainingFolder);
    menu->addSeparator();
    menu->addAction(tr("&Remove from List"), this, &GameList::RemoveFromList);
    menu->exec(QCursor::pos());
}

QAbstractItemView* GameList::GetActiveView() const {
    if (currentWidget() == m_grid) return m_grid;
    return m_list;
}

QSortFilterProxyModel* GameList::GetActiveProxyModel() const {
    if (currentWidget() == m_grid) return m_grid_proxy;
    return m_list_proxy;
}

QString GameList::GetSelectedGameId() const {
    const QAbstractItemView* view = GetActiveView();
    const QModelIndexList rows = view->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        const QModelIndexList idx = view->selectionModel()->selectedIndexes();
        if (idx.isEmpty()) return {};
        return GetActiveProxyModel()->mapToSource(idx.first()).siblingAtColumn(0).data(rg::GameListModel::ID_ROLE).toString();
    }
    return GetActiveProxyModel()->mapToSource(rows.first()).siblingAtColumn(0).data(rg::GameListModel::ID_ROLE).toString();
}

void GameList::SelectGame(const QString& id) {
    const int row = m_model->rowOf(id);
    if (row < 0) return;
    for (auto [view, proxy] : {std::pair<QAbstractItemView*, QSortFilterProxyModel*>{m_list, m_list_proxy}, {m_grid, m_grid_proxy}}) {
        const QModelIndex pi = proxy->mapFromSource(m_model->index(row, 0));
        if (!pi.isValid()) continue;
        view->selectionModel()->select(pi, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->setCurrentIndex(pi);
    }
}

void GameList::SetViewColumn(int col, bool view) {
    m_list->setColumnHidden(col, !view);
    OnHeaderViewChanged();
}

bool GameList::IsColumnVisible(int col) const { return !m_list->isColumnHidden(col); }

void GameList::SetPreferredView(bool list) {
    m_prefer_list = list;
    settings().setValue(QStringLiteral("gamelist/prefer_list"), list);
    ConsiderViewChange();
}

void GameList::ConsiderViewChange() {
    if (m_model->rowCount() > 0) setCurrentWidget(m_prefer_list ? static_cast<QWidget*>(m_list) : m_grid);
    else setCurrentWidget(m_empty);
}

void GameList::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !GetSelectedGameId().isEmpty()) {
        if (event->modifiers() == Qt::AltModifier) emit OpenProperties();
        else emit GameSelected();
    } else {
        QStackedWidget::keyPressEvent(event);
    }
}

void GameList::OnHeaderViewChanged() {
    static bool block = false;
    if (block) return;
    block = true;
    // Dolphin: "Qt's way of resizing stuff around stretched columns sucks" - de vrije ruimte wordt over de
    // instelbare kolommen verdeeld naar hun huidige verhouding.
    int available_width = width() - style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    int previous_width = 0;
    std::vector<int> candidate_columns;
    for (int i = 0; i < static_cast<int>(Column::Count); i++) {
        if (m_list->isColumnHidden(i)) continue;
        if (m_list->horizontalHeader()->sectionResizeMode(i) == QHeaderView::Fixed) available_width -= m_list->columnWidth(i);
        else {
            candidate_columns.push_back(i);
            previous_width += m_list->columnWidth(i);
        }
    }
    if (previous_width > 0)
        for (int column : candidate_columns) {
            const int column_width = static_cast<int>(
                std::max(5.f, std::ceil(available_width * (static_cast<float>(m_list->columnWidth(column)) / previous_width))));
            m_list->setColumnWidth(column, column_width);
        }
    block = false;
}

void GameList::SetSearchTerm(const QString& term) {
    m_model->SetSearchTerm(term);
    m_list_proxy->invalidate();
    m_grid_proxy->invalidate();
    UpdateGameCount();
}

void GameList::UpdateGameCount() { emit GameCountUpdated(m_model->rowCount(), m_list_proxy->rowCount()); }

void GameList::ZoomIn() {
    m_model->setScale(m_model->scale() + 0.1);
    m_list_proxy->invalidate();
    m_grid_proxy->invalidate();
    UpdateFont();
}

void GameList::ZoomOut() {
    if (m_model->scale() <= 0.5) return;
    m_model->setScale(m_model->scale() - 0.1);
    m_list_proxy->invalidate();
    m_grid_proxy->invalidate();
    UpdateFont();
}

void GameList::UpdateFont() {
    QFont f;
    f.setPointSizeF(m_model->scale() * f.pointSizeF());
    m_grid->setFont(f);
    const int s = static_cast<int>(128 * m_model->scale());
    m_grid->setIconSize(QSize(s, s));
    m_grid->setGridSize(QSize(s + 56, s + 56));
}

}  // namespace rg::ui
