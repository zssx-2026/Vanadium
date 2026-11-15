package inv.report;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Minimal JSON reader/writer (RFC 8259 subset). java.base only, no third-party dependencies.
 * Parsing errors carry a line/column position so corrupt metadata can be reported clearly.
 */
public final class Json {

  public enum Type { NULL, BOOL, INT, DOUBLE, STRING, ARRAY, OBJECT }

  private final Type type;
  private final boolean boolValue;
  private final long longValue;
  private final double doubleValue;
  private final String stringValue;
  private final List<Json> arrayValue;
  private final Map<String, Json> objectValue;

  private Json(Type type, boolean b, long i, double d, String s, List<Json> arr, Map<String, Json> obj) {
    this.type = type;
    this.boolValue = b;
    this.longValue = i;
    this.doubleValue = d;
    this.stringValue = s;
    this.arrayValue = arr;
    this.objectValue = obj;
  }

  public static Json ofNull() {
    return new Json(Type.NULL, false, 0L, 0.0, null, null, null);
  }

  public static Json of(boolean v) {
    return new Json(Type.BOOL, v, 0L, 0.0, null, null, null);
  }

  public static Json of(long v) {
    return new Json(Type.INT, false, v, 0.0, null, null, null);
  }

  public static Json of(double v) {
    return new Json(Type.DOUBLE, false, 0L, v, null, null, null);
  }

  public static Json of(String v) {
    return new Json(Type.STRING, false, 0L, 0.0, v == null ? "" : v, null, null);
  }

  public static Json array() {
    return new Json(Type.ARRAY, false, 0L, 0.0, null, new ArrayList<>(), null);
  }

  public static Json object() {
    return new Json(Type.OBJECT, false, 0L, 0.0, null, null, new LinkedHashMap<>());
  }

  public Type type() {
    return type;
  }

  public boolean isNull() {
    return type == Type.NULL;
  }

  public boolean isObject() {
    return type == Type.OBJECT;
  }

  public boolean isArray() {
    return type == Type.ARRAY;
  }

  public boolean isString() {
    return type == Type.STRING;
  }

  public boolean isNumber() {
    return type == Type.INT || type == Type.DOUBLE;
  }

  public boolean isBool() {
    return type == Type.BOOL;
  }

  public Json add(Json v) {
    arrayValue.add(v);
    return this;
  }

  public Json put(String key, Json v) {
    objectValue.put(key, v);
    return this;
  }

  public Json put(String key, String v) {
    return put(key, of(v));
  }

  public Json put(String key, long v) {
    return put(key, of(v));
  }

  public Json put(String key, double v) {
    return put(key, of(v));
  }

  public Json put(String key, boolean v) {
    return put(key, of(v));
  }

  public List<Json> items() {
    return arrayValue == null ? List.of() : arrayValue;
  }

  public Map<String, Json> fields() {
    return objectValue == null ? Map.of() : objectValue;
  }

  public int size() {
    if (type == Type.ARRAY) {
      return arrayValue.size();
    }
    if (type == Type.OBJECT) {
      return objectValue.size();
    }
    return 0;
  }

  public boolean has(String key) {
    return objectValue != null && objectValue.containsKey(key);
  }

  public Json get(String key) {
    return objectValue == null ? null : objectValue.get(key);
  }

  public String str(String key, String def) {
    Json v = get(key);
    return v != null && v.isString() ? v.stringValue : def;
  }

  public long num(String key, long def) {
    Json v = get(key);
    return v == null ? def : v.asLong(def);
  }

  public double dbl(String key, double def) {
    Json v = get(key);
    return v == null ? def : v.asDouble(def);
  }

  public boolean bool(String key, boolean def) {
    Json v = get(key);
    return v == null ? def : v.asBool(def);
  }

  public String asString(String def) {
    return type == Type.STRING ? stringValue : def;
  }

  public long asLong(long def) {
    return switch (type) {
      case INT -> longValue;
      case DOUBLE -> (long) doubleValue;
      case BOOL -> boolValue ? 1L : 0L;
      case STRING -> parseLongOr(stringValue, def);
      default -> def;
    };
  }

  public double asDouble(double def) {
    return switch (type) {
      case INT -> (double) longValue;
      case DOUBLE -> doubleValue;
      case BOOL -> boolValue ? 1.0 : 0.0;
      case STRING -> parseDoubleOr(stringValue, def);
      default -> def;
    };
  }

  public boolean asBool(boolean def) {
    return switch (type) {
      case BOOL -> boolValue;
      case INT -> longValue != 0L;
      case STRING -> "true".equalsIgnoreCase(stringValue) || "1".equals(stringValue) || "yes".equalsIgnoreCase(stringValue);
      default -> def;
    };
  }

  private static long parseLongOr(String s, long def) {
    try {
      return Long.parseLong(s.trim());
    } catch (RuntimeException e) {
      return def;
    }
  }

  private static double parseDoubleOr(String s, double def) {
    try {
      return Double.parseDouble(s.trim());
    } catch (RuntimeException e) {
      return def;
    }
  }

  /** Serialize. indent < 0 means compact output. */
  public String dump(int indent) {
    StringBuilder sb = new StringBuilder();
    write(sb, indent, 0);
    return sb.toString();
  }

  private void write(StringBuilder sb, int indent, int depth) {
    switch (type) {
      case NULL -> sb.append("null");
      case BOOL -> sb.append(boolValue ? "true" : "false");
      case INT -> sb.append(longValue);
      case DOUBLE -> sb.append(formatDouble(doubleValue));
      case STRING -> escape(sb, stringValue);
      case ARRAY -> writeArray(sb, indent, depth);
      case OBJECT -> writeObject(sb, indent, depth);
    }
  }

  private void writeArray(StringBuilder sb, int indent, int depth) {
    if (arrayValue.isEmpty()) {
      sb.append("[]");
      return;
    }
    boolean pretty = indent >= 0;
    sb.append('[');
    for (int k = 0; k < arrayValue.size(); k++) {
      if (k > 0) {
        sb.append(',');
      }
      if (pretty) {
        sb.append('\n');
        pad(sb, indent, depth + 1);
      }
      arrayValue.get(k).write(sb, indent, depth + 1);
    }
    if (pretty) {
      sb.append('\n');
      pad(sb, indent, depth);
    }
    sb.append(']');
  }

  private void writeObject(StringBuilder sb, int indent, int depth) {
    if (objectValue.isEmpty()) {
      sb.append("{}");
      return;
    }
    boolean pretty = indent >= 0;
    sb.append('{');
    int k = 0;
    for (Map.Entry<String, Json> e : objectValue.entrySet()) {
      if (k++ > 0) {
        sb.append(',');
      }
      if (pretty) {
        sb.append('\n');
        pad(sb, indent, depth + 1);
      }
      escape(sb, e.getKey());
      sb.append(pretty ? ": " : ":");
      e.getValue().write(sb, indent, depth + 1);
    }
    if (pretty) {
      sb.append('\n');
      pad(sb, indent, depth);
    }
    sb.append('}');
  }

  private static void pad(StringBuilder sb, int indent, int depth) {
    sb.append(" ".repeat(Math.max(0, indent * depth)));
  }

  private static String formatDouble(double d) {
    if (Double.isNaN(d) || Double.isInfinite(d)) {
      return "null";
    }
    if (d == Math.rint(d) && Math.abs(d) < 1.0e15) {
      return (long) d + ".0";
    }
    return Double.toString(d);
  }

  private static void escape(StringBuilder sb, String s) {
    sb.append('"');
    for (int k = 0; k < s.length(); k++) {
      char c = s.charAt(k);
      switch (c) {
        case '"' -> sb.append("\\\"");
        case '\\' -> sb.append("\\\\");
        case '\n' -> sb.append("\\n");
        case '\r' -> sb.append("\\r");
        case '\t' -> sb.append("\\t");
        case '\b' -> sb.append("\\b");
        case '\f' -> sb.append("\\f");
        default -> {
          if (c < 0x20) {
            sb.append(String.format(java.util.Locale.ROOT, "\\u%04x", (int) c));
          } else {
            sb.append(c);
          }
        }
      }
    }
    sb.append('"');
  }

  /** Raised for malformed JSON; the message includes line and column. */
  public static final class ParseException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public ParseException(String message) {
      super(message);
    }
  }

  public static Json parse(String text) {
    if (text == null) {
      throw new ParseException("no input");
    }
    Parser p = new Parser(text);
    Json v = p.parseValue();
    p.skipWhitespace();
    if (!p.atEnd()) {
      throw p.error("unexpected trailing content");
    }
    return v;
  }

  private static final class Parser {
    private final String s;
    private int i;

    Parser(String s) {
      this.s = s;
    }

    boolean atEnd() {
      return i >= s.length();
    }

    void skipWhitespace() {
      while (i < s.length()) {
        char c = s.charAt(i);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
          i++;
        } else {
          break;
        }
      }
    }

    ParseException error(String message) {
      int line = 1;
      int col = 1;
      int stop = Math.min(i, s.length());
      for (int k = 0; k < stop; k++) {
        if (s.charAt(k) == '\n') {
          line++;
          col = 1;
        } else {
          col++;
        }
      }
      return new ParseException("line " + line + ", column " + col + ": " + message);
    }

    Json parseValue() {
      skipWhitespace();
      if (atEnd()) {
        throw error("unexpected end of input");
      }
      char c = s.charAt(i);
      return switch (c) {
        case '{' -> parseObject();
        case '[' -> parseArray();
        case '"' -> Json.of(parseString());
        case 't' -> {
          expect("true");
          yield Json.of(true);
        }
        case 'f' -> {
          expect("false");
          yield Json.of(false);
        }
        case 'n' -> {
          expect("null");
          yield Json.ofNull();
        }
        default -> {
          if (c == '-' || (c >= '0' && c <= '9')) {
            yield parseNumber();
          }
          throw error("unexpected character '" + c + "'");
        }
      };
    }

    private void expect(String literal) {
      if (!s.startsWith(literal, i)) {
        throw error("expected '" + literal + "'");
      }
      i += literal.length();
    }

    private Json parseObject() {
      Json obj = Json.object();
      i++; // '{'
      skipWhitespace();
      if (!atEnd() && s.charAt(i) == '}') {
        i++;
        return obj;
      }
      while (true) {
        skipWhitespace();
        if (atEnd() || s.charAt(i) != '"') {
          throw error("expected object key string");
        }
        String key = parseString();
        skipWhitespace();
        if (atEnd() || s.charAt(i) != ':') {
          throw error("expected ':' after object key");
        }
        i++;
        obj.put(key, parseValue());
        skipWhitespace();
        if (atEnd()) {
          throw error("unterminated object");
        }
        char c = s.charAt(i);
        if (c == ',') {
          i++;
          continue;
        }
        if (c == '}') {
          i++;
          return obj;
        }
        throw error("expected ',' or '}' in object");
      }
    }

    private Json parseArray() {
      Json arr = Json.array();
      i++; // '['
      skipWhitespace();
      if (!atEnd() && s.charAt(i) == ']') {
        i++;
        return arr;
      }
      while (true) {
        arr.add(parseValue());
        skipWhitespace();
        if (atEnd()) {
          throw error("unterminated array");
        }
        char c = s.charAt(i);
        if (c == ',') {
          i++;
          continue;
        }
        if (c == ']') {
          i++;
          return arr;
        }
        throw error("expected ',' or ']' in array");
      }
    }

    private String parseString() {
      i++; // opening quote
      StringBuilder sb = new StringBuilder();
      while (true) {
        if (atEnd()) {
          throw error("unterminated string");
        }
        char c = s.charAt(i++);
        if (c == '"') {
          return sb.toString();
        }
        if (c < 0x20) {
          throw error("unescaped control character in string");
        }
        if (c != '\\') {
          sb.append(c);
          continue;
        }
        if (atEnd()) {
          throw error("unterminated escape sequence");
        }
        char e = s.charAt(i++);
        switch (e) {
          case '"' -> sb.append('"');
          case '\\' -> sb.append('\\');
          case '/' -> sb.append('/');
          case 'b' -> sb.append('\b');
          case 'f' -> sb.append('\f');
          case 'n' -> sb.append('\n');
          case 'r' -> sb.append('\r');
          case 't' -> sb.append('\t');
          case 'u' -> {
            if (i + 4 > s.length()) {
              throw error("truncated \\u escape");
            }
            String hex = s.substring(i, i + 4);
            try {
              sb.append((char) Integer.parseInt(hex, 16));
            } catch (NumberFormatException ex) {
              throw error("invalid \\u escape '" + hex + "'");
            }
            i += 4;
          }
          default -> throw error("invalid escape '\\" + e + "'");
        }
      }
    }

    private Json parseNumber() {
      int start = i;
      if (s.charAt(i) == '-') {
        i++;
      }
      boolean isDouble = false;
      int digits = 0;
      while (!atEnd() && s.charAt(i) >= '0' && s.charAt(i) <= '9') {
        i++;
        digits++;
      }
      if (!atEnd() && s.charAt(i) == '.') {
        isDouble = true;
        i++;
        while (!atEnd() && s.charAt(i) >= '0' && s.charAt(i) <= '9') {
          i++;
          digits++;
        }
      }
      if (!atEnd() && (s.charAt(i) == 'e' || s.charAt(i) == 'E')) {
        isDouble = true;
        i++;
        if (!atEnd() && (s.charAt(i) == '+' || s.charAt(i) == '-')) {
          i++;
        }
        while (!atEnd() && s.charAt(i) >= '0' && s.charAt(i) <= '9') {
          i++;
        }
      }
      String num = s.substring(start, i);
      if (digits == 0) {
        throw error("invalid number '" + num + "'");
      }
      if (!isDouble) {
        try {
          return Json.of(Long.parseLong(num));
        } catch (NumberFormatException ignored) {
          // fall through to double
        }
      }
      try {
        return Json.of(Double.parseDouble(num));
      } catch (NumberFormatException ex) {
        throw error("invalid number '" + num + "'");
      }
    }
  }
}
