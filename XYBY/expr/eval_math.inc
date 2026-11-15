// ============================================================
// 数学运算：[ADD] [SUB] [MUL] [DIV] [IDIV] [POW]
// ============================================================
std::string apply_math_op(const std::string& left, const std::string& op, const std::string& right) {
    bool left_num  = is_numeric(left);
    bool right_num = is_numeric(right);

    if (op == "[ADD]") {
        // ---------- 列表合并 ----------
        if (left.rfind("LST:", 0) == 0 && right.rfind("LST:", 0) == 0) {
            std::string right_body = right.substr(4);
            if (right_body.empty()) return left;
            return left + std::string(1, '\x01') + right_body;
        }
        
        if (left_num && right_num) {
            double l = std::stod(left), r = std::stod(right);
            double res = l + r;
            int max_dec = std::max(get_decimal_places(left), get_decimal_places(right));
            if (max_dec > 0) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(max_dec) << res;
                return oss.str();
            }
            return format_double(res);
        } else {
            return left + right;
        }
    }
    else if (op == "[SUB]") {
        if (left_num && right_num) {
            double l = std::stod(left), r = std::stod(right);
            double res = l - r;
            int max_dec = std::max(get_decimal_places(left), get_decimal_places(right));
            if (max_dec > 0) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(max_dec) << res;
                return oss.str();
            }
            return format_double(res);
        } else return "Error: Type Mismatch";
    }
    else if (op == "[MUL]") {
        if (left_num && right_num) {
            double l = std::stod(left), r = std::stod(right);
            double res = l * r;
            int max_dec = std::max(get_decimal_places(left), get_decimal_places(right));
            if (max_dec > 0) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(max_dec) << res;
                return oss.str();
            }
            return format_double(res);
        } else return "Error: Type Mismatch";
    }
    else if (op == "[DIV]") {
        if (left_num && right_num) {
            if (std::stod(right) == 0) return "Error: Division by Zero";
            double l = std::stod(left), r = std::stod(right);
            double res = l / r;
            int max_dec = std::max(get_decimal_places(left), get_decimal_places(right));
            if (max_dec > 0) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(max_dec) << res;
                return oss.str();
            }
            return format_double(res);
        } else return "Error: Type Mismatch";
    }
    else if (op == "[IDIV]") {
        if (left_num && right_num) {
            double l = std::stod(left), r = std::stod(right);
            if (r == 0) return "Error: Division by Zero";
            double res = std::floor(l / r);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(0) << res;
            return oss.str();
        } else return "Error: Type Mismatch";
    }
    else if (op == "[POW]") {
        if (left_num && right_num) {
            double l = std::stod(left), r = std::stod(right);
            double res = std::pow(l, r);
            int max_dec = std::max(get_decimal_places(left), get_decimal_places(right));
            if (max_dec > 0) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(max_dec) << res;
                return oss.str();
            }
            return format_double(res);
        } else return "Error: Type Mismatch";
    }
    return "Error: Unknown Operation";
}