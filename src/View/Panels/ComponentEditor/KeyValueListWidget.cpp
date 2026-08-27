// src/View/Panels/ComponentEditor/KeyValueListWidget.cpp
#include "KeyValueListWidget.h"
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>




KeyValueListWidget::KeyValueListWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    rowsLayout = new QVBoxLayout();
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    outer->addLayout(rowsLayout);

    QPushButton* addBtn = new QPushButton("+ Add Property", this);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(addBtn, &QPushButton::clicked, this, [this]() { addRow(); });
    outer->addWidget(addBtn, 0, Qt::AlignLeft);
}




/*
 * Ladder, checked in order: array/object -> real JSON parse; true/false/null
 * -> literal; integer-looking -> number; float-looking -> number; anything
 * else -> plain string. No quoting required for ordinary text.
 */
QJsonValue KeyValueListWidget::smartParseValue(const QString& raw) {
    QString t = raw.trimmed();
    if (t.isEmpty()) return QJsonValue(QString());

    if (t.startsWith('[') || t.startsWith('{')) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(t.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
        }
        // Malformed mid-typing (e.g. "[1, 2") -- fall back to a plain
        // string rather than rejecting the keystroke outright.
        return QJsonValue(t);
    }

    if (t == "true")  return QJsonValue(true);
    if (t == "false") return QJsonValue(false);
    if (t == "null")  return QJsonValue();

    bool okInt = false;
    qint64 asInt = t.toLongLong(&okInt);
    if (okInt) return QJsonValue(static_cast<double>(asInt));

    bool okDouble = false;
    double asDouble = t.toDouble(&okDouble);
    if (okDouble) return QJsonValue(asDouble);

    return QJsonValue(t);
}




QString KeyValueListWidget::valueToDisplayString(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Array:
            return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
        case QJsonValue::Object:
            return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
        case QJsonValue::Bool:
            return v.toBool() ? "true" : "false";
        case QJsonValue::Double: {
            double d = v.toDouble();
            // Print whole numbers without a trailing ".0" for readability.
            if (d == static_cast<qint64>(d)) return QString::number(static_cast<qint64>(d));
            return QString::number(d);
        }
        case QJsonValue::Null:
            return "null";
        default:
            return v.toString();
    }
}




void KeyValueListWidget::addRow(const QString& key, const QJsonValue& val) {
    QWidget* rowWidget = new QWidget(this);
    QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 2, 0, 2);

    QLineEdit* keyEdit = new QLineEdit(key, rowWidget);
    keyEdit->setPlaceholderText("key");

    QLineEdit* valueEdit = new QLineEdit(rowWidget);
    valueEdit->setPlaceholderText("value");
    valueEdit->setText(valueToDisplayString(val));

    QPushButton* removeBtn = new QPushButton("✖", rowWidget);
    removeBtn->setFixedSize(20, 20);
    removeBtn->setStyleSheet("border: none; color: #E53935;");

    rowLayout->addWidget(keyEdit, 1);
    rowLayout->addWidget(valueEdit, 2);
    rowLayout->addWidget(removeBtn);

    rowsLayout->addWidget(rowWidget);
    rows.append({rowWidget, keyEdit, valueEdit});

    connect(keyEdit, &QLineEdit::editingFinished, this, [this]() { emit changed(); });
    connect(valueEdit, &QLineEdit::editingFinished, this, [this]() { emit changed(); });
    connect(removeBtn, &QPushButton::clicked, this, [this, rowWidget]() { removeRow(rowWidget); });

    emit changed();
}




void KeyValueListWidget::removeRow(QWidget* rowWidget) {
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i].rowWidget == rowWidget) {
            rows.removeAt(i);
            break;
        }
    }
    rowWidget->deleteLater();
    emit changed();
}




void KeyValueListWidget::setValue(const QJsonObject& obj) {
    // Clear existing rows.
    for (const Row& row : rows) {
        row.rowWidget->deleteLater();
    }
    rows.clear();

    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        addRow(it.key(), it.value());
    }
}




QJsonObject KeyValueListWidget::value() const {
    QJsonObject result;
    for (const Row& row : rows) {
        QString key = row.keyEdit->text().trimmed();
        if (key.isEmpty()) continue; // skip incomplete rows rather than error
        result[key] = smartParseValue(row.valueEdit->text());
    }
    return result;
}
