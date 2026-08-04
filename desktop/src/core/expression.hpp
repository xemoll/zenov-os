#pragma once

#include <QString>

namespace zenapps {

struct ExpressionResult {
    bool ok = false;
    double value = 0.0;
    QString formatted;
    QString error;
};

ExpressionResult evaluateExpression(const QString& expression);

}  // namespace zenapps
