// src/View/Panels/ComponentEditor/KeyValueListWidget.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>

/*
 * A freeform "+ Add Property" key/value list -- no prefixed key list, no
 * autocomplete, user writes both sides. Used identically for specs, pins,
 * and emulator parameters, since all three are freeform-by-design.
 *
 * Starts empty; a row only exists once the user adds it (never pre-lists
 * every possible field) -- deliberate, matches the rest of this editor.
 *
 * Value parsing is "smart": committing a row's value text runs it through
 * smartParseValue() below, so the user never has to hand-quote strings but
 * can still express numbers, booleans, null, and full nested arrays/objects
 * just by typing valid JSON for those cases.
 */
class KeyValueListWidget : public QWidget {
    Q_OBJECT

public:
    explicit KeyValueListWidget(QWidget* parent = nullptr);

    void setValue(const QJsonObject& obj);
    QJsonObject value() const;

    // Exposed for reuse by anything else that wants the same smart-typing
    // behavior on a single QLineEdit without the full list widget.
    static QJsonValue smartParseValue(const QString& raw);
    static QString valueToDisplayString(const QJsonValue& v);

signals:
    void changed();

private:
    struct Row {
        QWidget* rowWidget;
        QLineEdit* keyEdit;
        QLineEdit* valueEdit;
    };

    QVBoxLayout* rowsLayout;
    QList<Row> rows;

    void addRow(const QString& key = QString(), const QJsonValue& val = QJsonValue());
    void removeRow(QWidget* rowWidget);
};
