#include "ComponentsPanel.h"
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QLabel>
#include <QGroupBox>
#include <QGridLayout>
#include <QFileInfo>
#include <QHBoxLayout>
#include "Document/Components/LibraryManager.h"
#include "Document/Components/ComponentBlueprint.h"
#include "View/Windows/EditorWindow.h"
#include "View/Windows/ComponentEditorWindow.h"
#include "Document/Components/ComponentData.h"

/*
 * Constructs a draggable widget representing a single component.
 * Displays either the loaded thumbnail or a fallback text identifier.
 */
DraggableComponentWidget::DraggableComponentWidget(const QString& modelId, const QString& modelName, const QString& iconPath, QWidget* parent)
    : QWidget(parent), modelId(modelId), modelName(modelName) {

    setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(2);

    QLabel* iconLabel = new QLabel(this);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    iconLabel->setFixedSize(64, 64);
    iconLabel->setStyleSheet("background-color: #27282b; border-radius: 3px;");
    iconLabel->setAlignment(Qt::AlignCenter);

    QPixmap pixmap(iconPath);
    if (!pixmap.isNull()) {
        iconLabel->setPixmap(pixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        iconLabel->setText("IMG");
    }

    QLabel* nameLabel = new QLabel(this);
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    nameLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    nameLabel->setWordWrap(true);

    QFont font = nameLabel->font();
    font.setPointSize(font.pointSize() + 3);
    nameLabel->setFont(font);

    QFontMetrics metrics(nameLabel->font());
    int maxTextWidthPixels = 86 * 2;
    QString elidedText = metrics.elidedText(modelName, Qt::ElideRight, maxTextWidthPixels);
    nameLabel->setText(elidedText);
    nameLabel->setFixedHeight((metrics.lineSpacing() * 2) + 2);

    layout->addWidget(iconLabel, 0, Qt::AlignHCenter);
    layout->addWidget(nameLabel, 0, Qt::AlignHCenter);

    setFixedSize(96, 125);
    setCursor(Qt::OpenHandCursor);
    setToolTip(QString("<b>%1</b><br>ID: %2").arg(modelName, modelId));
}




/*
 * Captures the initial mouse click position.
 * Explicitly accepts the event to prevent the parent scroll area from stealing focus.
 */
void DraggableComponentWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragStartPosition = event->pos();
        dragged = false;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}




/*
 * Resets the cursor if the user clicks but releases without initiating a drag.
 */
void DraggableComponentWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        setCursor(Qt::OpenHandCursor);
        if (!dragged) emit clicked(modelId);
        event->accept();
    }
}




/*
 * Detects sufficient mouse movement to trigger a drag operation.
 * Packages the component ID and renders a floating visual clone of the widget.
 */
void DraggableComponentWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - dragStartPosition).manhattanLength() < QApplication::startDragDistance()) return;

    EditorWindow* mainWindow = qobject_cast<EditorWindow*>(this->window());
    if (mainWindow) {
        emit mainWindow->componentLibDragStart(modelId);
    }

    dragged = true;
    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;
    mimeData->setData("application/rs-component", modelId.toUtf8());
    drag->setMimeData(mimeData);

    QPixmap pixmap = this->grab();
    drag->setPixmap(pixmap);
    drag->setHotSpot(event->pos());

    drag->exec(Qt::CopyAction);

    if (mainWindow) {
        emit mainWindow->componentLibDragEnd();
    }

    setCursor(Qt::OpenHandCursor);
    event->accept();
}




/*
 * Returns the underlying component identifier.
 * Used primarily for layout filtering logic during user searches.
 */
QString DraggableComponentWidget::getModelId() const {
    return modelId;
}

QString DraggableComponentWidget::getModelName() const {
    return modelName;
}

void DraggableComponentWidget::setSelected(bool selected) {
    setStyleSheet(selected
        ? "DraggableComponentWidget { border: 1px solid #6a6c73; border-radius: 3px; }"
        : "");
}




/*
 * Initializes the layout, search box, and scrollable area for the library view.
 * Binds the search input to the live filtering logic.
 */
ComponentsPanel::ComponentsPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search components...");

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* scrollContent = new QWidget(scrollArea);
    scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(scrollContent);


    mainLayout->addWidget(scrollArea);

    addButton = new QPushButton("Add Component", this);
    openButton = new QPushButton("Open", this);
    openButton->setEnabled(false);
    openButton->setToolTip("Open documentation for the selected component");

    QHBoxLayout* bottomRow = new QHBoxLayout();
    bottomRow->addWidget(searchBox, 1);
    bottomRow->addWidget(addButton);
    bottomRow->addWidget(openButton);
    mainLayout->addLayout(bottomRow);

    connect(addButton, &QPushButton::clicked, this, [this]() {
        ComponentData data;   // blank template
        ComponentEditorWindow* dialog = new ComponentEditorWindow(data, window());
        dialog->show();
    });
    connect(openButton, &QPushButton::clicked, this, &ComponentsPanel::openDocs);

    connect(searchBox, &QLineEdit::textChanged, this, &ComponentsPanel::filterComponents);
}




/*
 * Safely removes all existing category blocks and widgets from the panel.
 * Prevents memory leaks and layout duplication when reloading the library.
 */
void ComponentsPanel::clearPanel() {
    selectedModelId.clear();
    openButton->setEnabled(false);
    for (const CategoryBlock& block : categories) {
        if (block.container) block.container->deleteLater();
    }
    categories.clear();
}




/*
 * Builds the UI entirely from LibraryManager's pre-parsed category data.
 * Retrieves blueprints dynamically to fetch pre-resolved icon paths and names.
 */
void ComponentsPanel::loadCatalog() {
    clearPanel();

    const QList<CategoryDef>& libraryCategories = LibraryManager::getInstance().getCategories();
    if(libraryCategories.isEmpty()) return;

    for (const CategoryDef& catDef : libraryCategories) {
        CategoryBlock block;
        block.name = catDef.name;
        block.keys = catDef.keys;

        QGroupBox* group = new QGroupBox(block.name, this);
        QGridLayout* grid = new QGridLayout(group);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        int row = 0, col = 0, maxCols = 3;

        for (const QString& modelId : catDef.modelIds) {
            QString iconPath;
            ComponentBlueprint* bp = LibraryManager::getInstance().getBlueprint(modelId);
            if (bp) {
                iconPath = bp->meta.iconPath;
            }

            DraggableComponentWidget* itemWidget = new DraggableComponentWidget(modelId, bp->meta.name, iconPath, group);
            connect(itemWidget, &DraggableComponentWidget::clicked, this, &ComponentsPanel::selectComponent);
            grid->addWidget(itemWidget, row, col);
            block.items.append(itemWidget);

            if (++col >= maxCols) { col = 0; row++; }
        }

        block.container = group;
        categories.append(block);
        scrollLayout->addWidget(group);
    }
}




/*
 * Evaluates search input against category metadata and individual item IDs.
 * Hides non-matching items and fully collapses empty category containers.
 */
void ComponentsPanel::filterComponents(const QString& searchText) {
    QString query = searchText.toLower();

    for (const CategoryBlock& block : categories) {
        bool catMatch = block.name.toLower().contains(query);
        for (const QString& key : block.keys) {
            if (key.contains(query)) { catMatch = true; break; }
        }

        bool hasVisible = false;
        for (DraggableComponentWidget* item : block.items) {
            bool show = query.isEmpty() || catMatch || item->getModelId().toLower().contains(query);
            item->setVisible(show);
            hasVisible |= show;
        }

        block.container->setVisible(hasVisible);
    }
}




/*
 * Marks one component as selected (highlight + enables the Open button).
 */
void ComponentsPanel::selectComponent(const QString& modelId) {
    selectedModelId = modelId;
    for (const CategoryBlock& block : categories)
        for (DraggableComponentWidget* item : block.items)
            item->setSelected(item->getModelId() == modelId);
    openButton->setEnabled(!modelId.isEmpty());
}




/*
 * Opens the documentation window for the selected component.
 * Placeholder: the window is blank until the docs system exists.
 */
void ComponentsPanel::openDocs() {
    if (selectedModelId.isEmpty()) return;

    QString title = selectedModelId;
    if (ComponentBlueprint* bp = LibraryManager::getInstance().getBlueprint(selectedModelId))
        title = bp->meta.name;

    QWidget* docs = new QWidget(window(), Qt::Window);
    docs->setAttribute(Qt::WA_DeleteOnClose);
    docs->setWindowTitle("Docs - " + title);
    docs->resize(800, 600);
    docs->show();
}
