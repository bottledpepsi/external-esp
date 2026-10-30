package mcradar;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Just enough JSON for esp_structure.json: objects (LinkedHashMap), arrays (ArrayList), strings, numbers (Long or Double), true/false/null. */
final class MiniJson {
    private final String s;
    private int i;
    private MiniJson(String s) { this.s = s; }

    static Object parse(String text) {
        MiniJson p = new MiniJson(text);
        p.ws();
        Object v = p.value();
        p.ws();
        if (p.i != p.s.length()) throw new IllegalArgumentException("trailing text at " + p.i);
        return v;
    }

    @SuppressWarnings("unchecked") static Map<String, Object> obj(Object o) { return o instanceof Map ? (Map<String, Object>) o : new LinkedHashMap<String, Object>(); }
    @SuppressWarnings("unchecked") static List<Object> arr(Object o) { return o instanceof List ? (List<Object>) o : new ArrayList<Object>(); }
    static String str(Object o) { return o instanceof String ? (String) o : null; }

    private void ws() { while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++; }
    private char peek() { if (i >= s.length()) throw new IllegalArgumentException("unexpected end"); return s.charAt(i); }

    private Object value() {
        char c = peek();
        if (c == '{') {
            Map<String, Object> m = new LinkedHashMap<String, Object>();
            i++; ws();
            if (peek() == '}') { i++; return m; }
            for (;;) {
                ws(); String k = string(); ws();
                if (s.charAt(i++) != ':') throw new IllegalArgumentException("':' expected at " + i);
                ws(); m.put(k, value()); ws();
                char d = s.charAt(i++);
                if (d == '}') return m;
                if (d != ',') throw new IllegalArgumentException("',' or '}' expected at " + i);
            }
        }
        if (c == '[') {
            List<Object> l = new ArrayList<Object>();
            i++; ws();
            if (peek() == ']') { i++; return l; }
            for (;;) {
                ws(); l.add(value()); ws();
                char d = s.charAt(i++);
                if (d == ']') return l;
                if (d != ',') throw new IllegalArgumentException("',' or ']' expected at " + i);
            }
        }
        if (c == '"') return string();
        if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
        if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
        if (s.startsWith("null", i)) { i += 4; return null; }
        int st = i;
        while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
        String n = s.substring(st, i);
        if (n.isEmpty()) throw new IllegalArgumentException("value expected at " + st);
        return n.indexOf('.') < 0 && n.indexOf('e') < 0 && n.indexOf('E') < 0 ? (Object) Long.valueOf(n) : (Object) Double.valueOf(n);
    }

    private String string() {
        if (s.charAt(i++) != '"') throw new IllegalArgumentException("string expected at " + i);
        StringBuilder b = new StringBuilder();
        for (;;) {
            char c = s.charAt(i++);
            if (c == '"') return b.toString();
            if (c == '\\') {
                char e = s.charAt(i++);
                if (e == 'u') { b.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); i += 4; }
                else b.append(e == 'n' ? '\n' : e == 't' ? '\t' : e);
            } else b.append(c);
        }
    }
}
