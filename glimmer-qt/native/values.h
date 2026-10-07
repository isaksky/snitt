#pragma once
// Included after the session/handle registry. All operations run on the GUI thread.
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaProperty>
#include <QMetaEnum>
#include <QColor>
#include <QRectF>
#include <QUrl>
#include <cmath>
#include <climits>
#include <cstring>
#include <stdexcept>

namespace Values {
static void reject(const char *why) { throw std::runtime_error(why); }
static QString text(const QJsonValue &v) {
    if (!v.isString()) reject("Malformed Qt value: expected string");
    return v.toString();
}
static qint64 integer(const QJsonValue &v) {
    bool ok = false;
    const auto s = text(v);
    qint64 n = s.toLongLong(&ok);
    if (!ok || QString::number(n) != s) reject("Qt integer is outside int64 or malformed");
    return n;
}
static double real(const QJsonValue &v) {
    bool ok = false;
    double n = text(v).toDouble(&ok);
    if (!ok || !std::isfinite(n)) reject("Qt number must be finite");
    return n;
}
static QJsonArray array(const QJsonValue &v) {
    if (!v.isArray()) reject("Malformed Qt value: expected array");
    return v.toArray();
}
using References = QHash<QObject *, Handle>;
static QVariant decode(const QJsonValue &node, int depth = 0, References *references = nullptr) {
    if (depth > 64) reject("Qt value nesting exceeds 64");
    auto a = array(node);
    if (a.size() != 2) reject("Malformed Qt value");
    auto tag = text(a[0]);
    auto p = a[1];
    if (tag == "nil") return QVariant();
    if (tag == "string") return text(p);
    if (tag == "bool") {
        auto s = text(p);
        if (s != "0" && s != "1") reject("Invalid Qt boolean");
        return s == "1";
    }
    if (tag == "int") return QVariant::fromValue(integer(p));
    if (tag == "double") return real(p);
    if (tag == "color") {
        QColor color(text(p));
        if (!color.isValid()) reject("Invalid Qt color");
        return color;
    }
    if (tag == "url" || tag == "file") {
        // Empty QUrl is Qt's explicit "no source" value for media and images.
        if (tag == "url" && text(p).isEmpty()) return QUrl();
        QUrl url = tag == "file" ? QUrl::fromLocalFile(text(p)) : QUrl(text(p), QUrl::StrictMode);
        if (!url.isValid()) reject("Invalid Qt URL");
        return url;
    }
    if (tag == "object") {
        qint64 id = integer(p);
        auto *object = id > 0 ? objectFor(Handle(id)) : nullptr;
        if (!object) reject("Invalid or disposed Qt object reference");
        if (references) references->insert(object, Handle(id));
        return QVariant::fromValue(object);
    }
    if (tag == "list") {
        QVariantList result;
        for (const auto &v : array(p)) result.append(decode(v, depth + 1, references));
        return result;
    }
    if (tag == "map") {
        QVariantMap result;
        for (const auto &v : array(p)) {
            auto pair = array(v);
            if (pair.size() != 2) reject("Malformed Qt map entry");
            auto key = text(pair[0]);
            if (result.contains(key)) reject("Duplicate Qt map key");
            result.insert(key, decode(pair[1], depth + 1, references));
        }
        return result;
    }
    bool integral = tag == "point" || tag == "size" || tag == "rect";
    if (integral || tag == "point-f" || tag == "size-f" || tag == "rect-f") {
        auto parts = array(p);
        int count = tag.startsWith("rect") ? 4 : 2;
        if (parts.size() != count) reject("Invalid Qt geometry arity");
        double n[4] = {};
        for (int i = 0; i < count; ++i) {
            if (integral) {
                qint64 x = integer(parts[i]);
                if (x < INT_MIN || x > INT_MAX) reject("Qt geometry exceeds int32");
                n[i] = double(x);
            } else n[i] = real(parts[i]);
        }
        if (tag == "point") return QPoint(int(n[0]), int(n[1]));
        if (tag == "size") return QSize(int(n[0]), int(n[1]));
        if (tag == "rect") return QRect(int(n[0]), int(n[1]), int(n[2]), int(n[3]));
        if (tag == "point-f") return QPointF(n[0], n[1]);
        if (tag == "size-f") return QSizeF(n[0], n[1]);
        return QRectF(n[0], n[1], n[2], n[3]);
    }
    reject("Unsupported Qt value type");
    return {};
}
static QJsonArray node(const char *tag, const QJsonValue &v) { return {QString::fromLatin1(tag), v}; }
static QJsonArray encode(const QVariant &v, int depth = 0, const References &references = {}, bool strictReferences = false) {
    if (depth > 64) reject("Qt value nesting exceeds 64");
    if (!v.isValid()) return node("nil", "");
    const int type = v.metaType().id();
    if (v.metaType().flags().testFlag(QMetaType::PointerToQObject)) {
        // Do not qobject_cast a potentially dangling pointer before consulting
        // the registry. QObject is the first base of Qt metaobject classes.
        QObject *object = nullptr;
        if (v.metaType().sizeOf() != sizeof(object)) reject("Unsupported Qt object pointer layout");
        std::memcpy(&object, v.constData(), sizeof(object));
        if (!object) return node("nil", "");
        Handle id = active->relay.identities.value(object);
        if (!id || active->relay.objects.value(id).data() != object ||
            ((strictReferences || references.contains(object)) && references.value(object) != id))
            reject("Unmanaged or disposed Qt object reference");
        return node("object", QString::number(id));
    }
    if (v.metaType().flags().testFlag(QMetaType::IsEnumeration)) {
        auto *meta = v.metaType().metaObject();
        const QByteArray typeName(v.metaType().name());
        const QByteArray shortName = typeName.mid(typeName.lastIndexOf(':') + 1);
        const int index = meta ? meta->indexOfEnumerator(shortName.constData()) : -1;
        if (index < 0) reject("Unsupported Qt enum metadata");
        const auto enumeration = meta->enumerator(index);
        const QByteArray keys = enumeration.isFlag() ? enumeration.valueToKeys(v.toInt())
                                                    : QByteArray(enumeration.valueToKey(v.toInt()));
        bool valid = false;
        int decoded = enumeration.isFlag() ? enumeration.keysToValue(keys.constData(), &valid)
                                           : enumeration.keyToValue(keys.constData(), &valid);
        if (enumeration.isFlag() && keys.isEmpty() && v.toInt() == 0) {
            valid = true; decoded = 0;
        }
        if (!valid || decoded != v.toInt()) reject("Unknown Qt enum value");
        QJsonArray names;
        if (!keys.isEmpty()) for (const auto &key : keys.split('|')) names.append(QString::fromUtf8(key));
        return node(enumeration.isFlag() ? "flags" : "enum", names);
    }
    switch (type) {
    case QMetaType::Bool: return node("bool", v.toBool() ? "1" : "0");
    case QMetaType::QString: return node("string", v.toString());
    case QMetaType::Int: case QMetaType::LongLong: case QMetaType::Short:
    case QMetaType::Long: case QMetaType::SChar:
        return node("int", QString::number(v.toLongLong()));
    case QMetaType::UInt: case QMetaType::ULongLong: case QMetaType::UShort:
    case QMetaType::ULong: case QMetaType::UChar:
        if (v.toULongLong() > quint64(LLONG_MAX)) reject("Qt unsigned integer exceeds int64");
        return node("int", QString::number(v.toULongLong()));
    case QMetaType::Double: case QMetaType::Float:
        if (!std::isfinite(v.toDouble())) reject("Qt number must be finite");
        return node("double", QString::number(v.toDouble(), 'g', 17));
    case QMetaType::QColor: return node("color", v.value<QColor>().name(QColor::HexArgb));
    case QMetaType::QUrl: return node("url", v.toUrl().toString(QUrl::FullyEncoded));
    case QMetaType::QPoint: { auto p = v.toPoint(); return node("point", QJsonArray{QString::number(p.x()), QString::number(p.y())}); }
    case QMetaType::QSize: { auto p = v.toSize(); return node("size", QJsonArray{QString::number(p.width()), QString::number(p.height())}); }
    case QMetaType::QRect: { auto p = v.toRect(); return node("rect", QJsonArray{QString::number(p.x()), QString::number(p.y()), QString::number(p.width()), QString::number(p.height())}); }
    case QMetaType::QPointF: { auto p = v.toPointF(); return node("point-f", QJsonArray{QString::number(p.x(), 'g', 17), QString::number(p.y(), 'g', 17)}); }
    case QMetaType::QSizeF: { auto p = v.toSizeF(); return node("size-f", QJsonArray{QString::number(p.width(), 'g', 17), QString::number(p.height(), 'g', 17)}); }
    case QMetaType::QRectF: { auto p = v.toRectF(); return node("rect-f", QJsonArray{QString::number(p.x(), 'g', 17), QString::number(p.y(), 'g', 17), QString::number(p.width(), 'g', 17), QString::number(p.height(), 'g', 17)}); }
    case QMetaType::QStringList: case QMetaType::QVariantList: {
        QJsonArray result;
        for (const auto &x : v.toList()) result.append(encode(x, depth + 1, references, strictReferences));
        return node("list", result);
    }
    case QMetaType::QVariantMap: {
        QJsonArray result;
        const auto map = v.toMap();
        for (auto i = map.cbegin(); i != map.cend(); ++i) result.append(QJsonArray{i.key(), encode(i.value(), depth + 1, references, strictReferences)});
        return node("map", result);
    }
    default: reject("Unsupported Qt property return type");
    }
    return {};
}
static QJsonArray parse(const char *wire) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(wire, &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) reject("Malformed Qt value JSON");
    auto result = doc.array();
    if (result.size() != 2) reject("Malformed Qt value");
    return result;
}
static QQmlProperty property(QObject *object, const char *name) {
    QQmlProperty prop(object, QString::fromUtf8(name), qmlContext(object));
    if (!prop.isValid() || !prop.isProperty()) reject("Missing Qt property");
    return prop;
}
static QVariant converted(QVariant v, QMetaType target) {
    if (target.id() == QMetaType::QVariant) return v;
    if (target.flags().testFlag(QMetaType::PointerToQObject)) {
        if (!v.isValid()) return QVariant(target, nullptr);
        if (!v.metaType().flags().testFlag(QMetaType::PointerToQObject)) reject("Qt property requires object reference");
        auto *object = v.value<QObject *>();
        if (!object->metaObject()->inherits(target.metaObject())) reject("Wrong Qt object reference class");
        if (!v.convert(target)) reject("Qt object conversion failed");
        return v;
    }
    if (v.metaType() == target) return v;
    if (v.metaType().id() == QMetaType::LongLong) {
        qint64 n = v.toLongLong();
        bool fits = false;
        switch (target.id()) {
        case QMetaType::Int: fits = n >= INT_MIN && n <= INT_MAX; break;
        case QMetaType::UInt: fits = n >= 0 && quint64(n) <= UINT_MAX; break;
        case QMetaType::LongLong: fits = true; break;
        case QMetaType::Double: fits = n >= -9007199254740992LL && n <= 9007199254740992LL; break;
        default: break;
        }
        if (fits && v.convert(target)) return v;
    }
    if (target.id() == QMetaType::QStringList && v.metaType().id() == QMetaType::QVariantList) {
        QStringList list;
        for (const auto &x : v.toList()) {
            if (x.metaType().id() != QMetaType::QString) reject("Qt string list requires strings");
            list.append(x.toString());
        }
        return list;
    }
    reject("Wrong Qt property value type or numeric range");
    return {};
}
static QVariant checked(const QQmlProperty &prop, const QJsonArray &wire, References &references) {
    if (!prop.isWritable()) reject("Read-only Qt property");
    auto meta = prop.property();
    if (meta.isEnumType()) {
        auto e = meta.enumerator();
        auto tag = text(wire[0]);
        if (tag != (e.isFlag() ? "flags" : "enum")) reject("Qt enum/flags requires symbolic keys");
        auto keys = array(wire[1]);
        if (!e.isFlag() && keys.size() != 1) reject("Qt enum requires one key");
        int value = 0;
        for (const auto &key : keys) {
            bool ok;
            auto bytes = text(key).toUtf8();
            if (bytes.contains('\0')) reject("Invalid Qt enum/flag key");
            int part = e.keyToValue(bytes.constData(), &ok);
            if (!ok) reject("Invalid Qt enum/flag key");
            value |= part;
        }
        return value;
    }
    return converted(decode(wire, 0, &references), QMetaType(prop.propertyType()));
}
// QVariant equality permits cross-type numeric/string conversion, including
// inside containers. Equal-write suppression must preserve the type as well.
static bool sameValue(const QVariant &a, const QVariant &b) {
    if (a.metaType() != b.metaType()) return false;
    if (a.metaType().id() == QMetaType::QVariantList) {
        const auto left = a.toList(), right = b.toList();
        if (left.size() != right.size()) return false;
        for (qsizetype i = 0; i < left.size(); ++i)
            if (!sameValue(left[i], right[i])) return false;
        return true;
    }
    if (a.metaType().id() == QMetaType::QVariantMap) {
        const auto left = a.toMap(), right = b.toMap();
        if (left.keys() != right.keys()) return false;
        for (auto i = left.cbegin(); i != left.cend(); ++i)
            if (!sameValue(i.value(), right.value(i.key()))) return false;
        return true;
    }
    return a == b;
}
static const char *result(const QJsonArray &v) {
    static thread_local QByteArray bytes;
    bytes = QJsonDocument(v).toJson(QJsonDocument::Compact);
    return bytes.constData();
}
}
extern "C" Q_DECL_EXPORT int gqt_write_value(Handle id, const char *name, const char *wire) {
    try {
        auto *object = objectFor(id);
        if (!object) return 0;
        auto prop = Values::property(object, name);
        Values::References references;
        auto value = Values::checked(prop, Values::parse(wire), references);
        // Equal writes preserve existing bindings, selection and cursor position.
        const auto current = prop.read();
        const bool equal = prop.property().isEnumType() ? current.toInt() == value.toInt() : Values::sameValue(current, value);
        if (!equal && !prop.write(value)) return fail("Qt rejected property write");
        active->relay.valueReferences[id].insert(QString::fromUtf8(name), references);
        return 1;
    } catch (const std::exception &e) { return fail(e.what()); }
}
extern "C" Q_DECL_EXPORT const char *gqt_read_value(Handle id, const char *name) {
    try {
        auto *object = objectFor(id);
        if (!object) return nullptr;
        auto prop = Values::property(object, name);
        if (!prop.property().isReadable()) Values::reject("Unreadable Qt property");
        auto value = prop.read();
        if (prop.property().isEnumType()) {
            auto e = prop.property().enumerator();
            QByteArray keys = e.isFlag() ? e.valueToKeys(value.toInt()) : QByteArray(e.valueToKey(value.toInt()));
            bool valid = false;
            int decoded = e.isFlag() ? e.keysToValue(keys.constData(), &valid) : e.keyToValue(keys.constData(), &valid);
            if (e.isFlag() && keys.isEmpty() && value.toInt() == 0) { valid = true; decoded = 0; }
            if (!valid || decoded != value.toInt()) Values::reject("Unknown Qt enum/flag value");
            QJsonArray names;
            if (!keys.isEmpty()) for (auto key : keys.split('|')) names.append(QString::fromUtf8(key));
            return Values::result(Values::node(e.isFlag() ? "flags" : "enum", names));
        }
        return Values::result(Values::encode(value, 0, active->relay.valueReferences.value(id).value(QString::fromUtf8(name))));
    } catch (const std::exception &e) { fail(e.what()); return nullptr; }
}
extern "C" Q_DECL_EXPORT int gqt_reset_property(Handle id, const char *name) {
    try {
        auto *object = objectFor(id);
        if (!object) return 0;
        auto prop = Values::property(object, name);
        if (!prop.isWritable()) Values::reject("Read-only Qt property");
        if (!prop.isResettable()) Values::reject("Qt property has no native RESET; supply an explicit default");
        if (!prop.reset()) return fail("Qt property RESET failed");
        active->relay.valueReferences[id].remove(QString::fromUtf8(name));
        return 1;
    } catch (const std::exception &e) { return fail(e.what()); }
}
extern "C" Q_DECL_EXPORT const char *gqt_local_path(const char *wire) {
    try {
        // This conversion uses no QObject and is worker-safe.
        auto a = Values::parse(wire);
        if (a[0] != "url" && a[0] != "file") Values::reject("Expected Qt URL");
        auto url = Values::decode(a).toUrl();
        if (!url.isLocalFile()) Values::reject("Qt URL is not a local file");
        return Values::result(Values::encode(url.toLocalFile()));
    } catch (const std::exception &e) { fail(e.what()); return nullptr; }
}
