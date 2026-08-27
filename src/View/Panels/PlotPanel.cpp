// src/UI/Docking/PlotPanel.cpp
#include "PlotPanel.h"
#include <QPushButton>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>

PlotPanel::PlotPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);

    tabWidget = new QTabWidget(this);
    tabWidget->setTabPosition(QTabWidget::West);
    panelLayout->addWidget(tabWidget);

    // Tab 1: I/O Plots (channel-based canvases)
    QWidget* ioTab = new QWidget();
    QVBoxLayout* outerLayout = new QVBoxLayout(ioTab);
    outerLayout->setContentsMargins(5, 5, 5, 5);
    outerLayout->setSpacing(8);

    // Scroll Area for Canvas Cards
    QScrollArea* scrollArea = new QScrollArea(ioTab);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* scrollContent = new QWidget();
    canvasListLayout = new QVBoxLayout(scrollContent);
    canvasListLayout->setContentsMargins(0, 0, 0, 0);
    canvasListLayout->setSpacing(10);
    canvasListLayout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea);

    // Bottom Pinned Add Button (Native Styling)
    QPushButton* addCanvasBtn = new QPushButton("+ Add Canvas");
    addCanvasBtn->setMinimumHeight(35);
    outerLayout->addWidget(addCanvasBtn);

    connect(addCanvasBtn, &QPushButton::clicked, this, &PlotPanel::showAddCanvasDialog);

    tabWidget->addTab(ioTab, "  I/O  ");

    // Tab 2: Body Motion (MuJoCo body velocity/acceleration)
    tabWidget->addTab(new MotionPanel(), "Motion");
}

void PlotPanel::showAddCanvasDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Add New Canvas");
    dialog.setMinimumSize(350, 200);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel("Canvas Name:"));
    QLineEdit* nameInput = new QLineEdit("New Plot");
    layout->addWidget(nameInput);

    layout->addWidget(new QLabel("Data Type:"));
    QComboBox* typeCombo = new QComboBox();
    typeCombo->addItem("Scalar (1D Time Series)", QVariant::fromValue(DataType::SCALAR));
    typeCombo->addItem("Vector (3D/Array)", QVariant::fromValue(DataType::VECTOR));
    typeCombo->addItem("Image (Camera View)", QVariant::fromValue(DataType::IMAGE));
    layout->addWidget(typeCombo);

    layout->addStretch();

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QString name = nameInput->text();
        DataType type = typeCombo->currentData().value<DataType>();

        CanvasDockItem* newCanvas = new CanvasDockItem(type, name, this);
        canvasListLayout->addWidget(newCanvas);
    }
}
