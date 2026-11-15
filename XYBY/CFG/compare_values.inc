// ============================================================
// 比较运算符逻辑：EQ / NE / GE / LE / GT / LT
// ============================================================
bool compare_values(const std::string& actual, const std::string& expected,
                    const std::string& op) {
    if (op == "[EQ]") return actual == expected;
    if (op == "[NE]") return actual != expected;

    if (is_numeric(actual) && is_numeric(expected)) {
        double l = std::stod(actual);
        double r = std::stod(expected);
        if      (op == "[GE]") return l >= r;
        else if (op == "[LE]") return l <= r;
        else if (op == "[GT]") return l >  r;
        else if (op == "[LT]") return l <  r;
    } else {
        if      (op == "[GE]") return actual >= expected;
        else if (op == "[LE]") return actual <= expected;
        else if (op == "[GT]") return actual >  expected;
        else if (op == "[LT]") return actual <  expected;
    }
    return false;
}