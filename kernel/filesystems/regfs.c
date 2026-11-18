#include "regfs.h"
#include "fs.h"
#include "heap.h"
#include "serial/serial.h"
#include <stddef.h>
#include <str.h>

static FSDriver g_regfs_driver = {
    .name = "regfs",
    .mount = regfs_mount,
    .umount = regfs_umount,
    .open = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .next = NULL,
};

static const char* g_hive_path = NULL;
static RegNode* node_alloc(const char* name);
static RegValue* value_alloc(const char* name, uint8_t type, void* data, uint32_t size);

void regfs_set_hive_path(const char* path) {
    g_hive_path = path;
}

void regfs_init(void) {
    fs_register_driver(&g_regfs_driver);
    serial_emergency_print("regfs: registered\n");
}

int regfs_sync(RegFS* rfs) {
    if (!rfs || !rfs->root) return -1;

    uint8_t* data = NULL;
    uint32_t size = 0;
    if (regfs_save_hive(rfs, &data, &size) != 0) {
        serial_emergency_print("regfs: save_hive failed\n");
        return -1;
    }

    File* f = NULL;
    FileSystem* backing_fs = fs_find_by_path("/");
    const char* backing_path = rfs->hive_path;
    while (*backing_path == '/') backing_path++;
    if (!backing_fs || !backing_fs->driver || !backing_fs->driver->open ||
        backing_fs->driver->open(backing_fs, backing_path,
                                 FS_WRITE | FS_CREATE | FS_TRUNCATE, &f) != 0) {
        serial_emergency_print("regfs: cannot open for write\n");
        kfree(data);
        return -1;
    }

    int64_t n = fs_write(f, data, size);
    fs_close(f);
    kfree(data);

    if (n != (int64_t)size) {
        serial_emergency_print("regfs: write failed\n");
        return -1;
    }

    rfs->dirty = 0;
    serial_emergency_print("regfs: synced, size=");
    serial_emergency_print_hex(size);
    serial_emergency_print("\n");
    return 0;
}

int regfs_mount(BlockDevice* dev, FileSystem** out) {
    (void)dev;

    if (!g_hive_path) {
        serial_emergency_print("regfs: hive path not set\n");
        return -1;
    }

    serial_emergency_print("regfs: mounting, hive=");
    serial_emergency_print(g_hive_path);
    serial_emergency_print("\n");
    File* f = NULL;
    if (fs_open(g_hive_path, FS_READ, &f) != 0) {
        serial_emergency_print("regfs: cannot open hive file\n");
        return -1;
    }

    uint32_t buf_size = 64 * 1024;
    uint8_t* buf = (uint8_t*)kmalloc(buf_size);
    if (!buf) { fs_close(f); return -1; }

    int64_t n = fs_read(f, buf, buf_size);
    fs_close(f);
    if (n <= 0) {
        serial_emergency_print("regfs: hive read failed\n");
        kfree(buf);
        return -1;
    }

    RegFS* rfs = (RegFS*)kzalloc(sizeof(RegFS));
    if (!rfs) { kfree(buf); return -1; }

    rfs->dev = NULL;
    rfs->hive_path = g_hive_path;
    rfs->dirty = 0;

    if (regfs_load_hive(rfs, buf, (uint32_t)n) != 0) {
        serial_emergency_print("regfs: hive parse failed\n");
        kfree(buf);
        kfree(rfs);
        return -1;
    }
    kfree(buf);

    (*out)->private = rfs;
    return 0;
}
void regfs_umount(FileSystem* fs) {
    if (!fs || !fs->private) return;
    RegFS* rfs = (RegFS*)fs->private;
    if (rfs->dirty) {
        regfs_sync(rfs);
    }
    kfree(rfs);
    fs->private = NULL;
}
RegNode* regfs_find_key(RegNode* root, const char* path) {
    if (!root || !path) return NULL;
    RegNode* cur = root;
    const char* p = path;
    if (*p == '/') p++;

    char comp[64];
    while (*p) {
        int n = 0;
        while (*p && *p != '/' && n < 63) comp[n++] = *p++;
        comp[n] = '\0';
        if (n == 0) break;

        RegNode* found = NULL;
        for (RegNode* ch = cur->first_child; ch; ch = ch->next_sibling) {
            if (strcmp(ch->name, comp) == 0) { found = ch; break; }
        }
        if (!found) return NULL;
        cur = found;
        if (*p == '/') p++;
    }
    return cur;
}

RegValue* regfs_find_value(RegNode* key, const char* name) {
    if (!key || !name) return NULL;
    for (RegValue* v = key->first_value; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return v;
    }
    return NULL;
}
uint32_t regfs_get_u32(RegFS* rfs, const char* path) {
    if (!rfs || !rfs->root || !path) return 0;
    char key_path[256];
    const char* name = NULL;
    int last_slash = -1;
    int i = 0;
    while (path[i] && i < 255) {
        if (path[i] == '/') last_slash = i;
        key_path[i] = path[i];
        i++;
    }
    key_path[i] = '\0';
    if (last_slash < 0) return 0;
    name = key_path + last_slash + 1;
    key_path[last_slash] = '\0';

    RegNode* key = regfs_find_key(rfs->root, key_path);
    if (!key) return 0;
    RegValue* v = regfs_find_value(key, name);
    if (!v || !v->data) return 0;
    if (v->type == REG_TYPE_UINT32 || v->type == REG_TYPE_HEX) {
        if (v->data_size >= 4) return *(uint32_t*)v->data;
    }
    return 0;
}

uint64_t regfs_get_u64(RegFS* rfs, const char* path) {
    if (!rfs || !rfs->root || !path) return 0;
    char key_path[256];
    const char* name = NULL;
    int last_slash = -1;
    int i = 0;
    while (path[i] && i < 255) {
        if (path[i] == '/') last_slash = i;
        key_path[i] = path[i];
        i++;
    }
    key_path[i] = '\0';
    if (last_slash < 0) return 0;
    name = key_path + last_slash + 1;
    key_path[last_slash] = '\0';

    RegNode* key = regfs_find_key(rfs->root, key_path);
    if (!key) return 0;
    RegValue* v = regfs_find_value(key, name);
    if (!v || !v->data) return 0;
    if (v->type == REG_TYPE_UINT64 && v->data_size >= 8) {
        return *(uint64_t*)v->data;
    }
    return 0;
}

int regfs_get_str(RegFS* rfs, const char* path, char* out, uint32_t out_size) {
    if (!rfs || !rfs->root || !path || !out || out_size == 0) return -1;
    char key_path[256];
    const char* name = NULL;
    int last_slash = -1;
    int i = 0;
    while (path[i] && i < 255) {
        if (path[i] == '/') last_slash = i;
        key_path[i] = path[i];
        i++;
    }
    key_path[i] = '\0';
    if (last_slash < 0) return -1;
    name = key_path + last_slash + 1;
    key_path[last_slash] = '\0';

    RegNode* key = regfs_find_key(rfs->root, key_path);
    if (!key) return -1;
    RegValue* v = regfs_find_value(key, name);
    if (!v || !v->data || v->type != REG_TYPE_STRING) return -1;

    uint32_t n = 0;
    while (n < out_size - 1 && n < v->data_size - 1 && ((char*)v->data)[n]) {
        out[n] = ((char*)v->data)[n];
        n++;
    }
    out[n] = '\0';
    return (int)n;
}

static int split_value_path(const char* path, char key_path[256], const char** name) {
    size_t length = strlen(path);
    if (!length || length >= 256) return -1;
    memcpy(key_path, path, length + 1);
    char* slash = NULL;
    for (size_t i = 0; i < length; i++) if (key_path[i] == '/') slash = key_path + i;
    if (!slash || !slash[1]) return -1;
    *name = slash + 1;
    *slash = '\0';
    return 0;
}

static RegNode* ensure_key(RegFS* rfs, const char* path) {
    RegNode* current = rfs->root;
    const char* part = path;
    if (*part == '/') part++;
    while (*part) {
        char name[HIVE_KEY_NAME_MAX];
        uint32_t length = 0;
        while (*part && *part != '/' && length + 1 < sizeof(name)) name[length++] = *part++;
        if (*part && *part != '/') return NULL;
        name[length] = '\0';
        if (!length) return NULL;
        RegNode* child = NULL;
        for (RegNode* scan = current->first_child; scan; scan = scan->next_sibling) {
            if (strcmp(scan->name, name) == 0) { child = scan; break; }
        }
        if (!child) {
            child = node_alloc(name);
            if (!child) return NULL;
            child->parent = current;
            RegNode** tail = &current->first_child;
            while (*tail) tail = &(*tail)->next_sibling;
            *tail = child;
        }
        current = child;
        if (*part == '/') part++;
    }
    return current;
}

static RegValue* ensure_value(RegNode* key, const char* name, uint8_t type) {
    RegValue* value = regfs_find_value(key, name);
    if (value) return value;
    uint32_t size = type == REG_TYPE_STRING ? 1 : sizeof(uint32_t);
    void* data = kzalloc(size);
    if (!data) return NULL;
    value = value_alloc(name, type, data, size);
    if (!value) { kfree(data); return NULL; }
    RegValue** tail = &key->first_value;
    while (*tail) tail = &(*tail)->next;
    *tail = value;
    return value;
}

int regfs_set_u32(RegFS* rfs, const char* path, uint32_t value) {
    char key_path[256];
    const char* name;
    if (!rfs || split_value_path(path, key_path, &name) != 0) return -1;
    RegNode* key = ensure_key(rfs, key_path);
    if (!key) return -1;
    RegValue* entry = ensure_value(key, name, REG_TYPE_UINT32);
    if (!entry || !entry->data || entry->data_size < sizeof(value) ||
        (entry->type != REG_TYPE_UINT32 && entry->type != REG_TYPE_HEX)) return -1;
    *(uint32_t*)entry->data = value;
    rfs->dirty = 1;
    return regfs_sync(rfs);
}

int regfs_set_str(RegFS* rfs, const char* path, const char* value) {
    char key_path[256];
    const char* name;
    if (!rfs || !value || split_value_path(path, key_path, &name) != 0) return -1;
    RegNode* key = ensure_key(rfs, key_path);
    if (!key) return -1;
    RegValue* entry = ensure_value(key, name, REG_TYPE_STRING);
    if (!entry || entry->type != REG_TYPE_STRING) return -1;
    size_t length = strlen(value) + 1;
    if (length > 4096) return -1;
    char* replacement = (char*)kmalloc(length);
    if (!replacement) return -1;
    memcpy(replacement, value, length);
    if (entry->data) kfree(entry->data);
    entry->data = replacement;
    entry->data_size = (uint32_t)length;
    rfs->dirty = 1;
    return regfs_sync(rfs);
}

static uint32_t append_u32(char* out, uint32_t value) {
    char reverse[11];
    uint32_t count = 0;
    do { reverse[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    for (uint32_t i = 0; i < count; i++) out[i] = reverse[count - i - 1];
    out[count] = '\0';
    return count;
}

int regfs_enum(RegFS* rfs, const char* path, uint32_t index, RegEntryInfo* out) {
    if (!rfs || !out) return -1;
    RegNode* key = regfs_find_key(rfs->root, path);
    if (!key) return -1;
    uint32_t child_count = 0;
    for (RegNode* child = key->first_child; child; child = child->next_sibling) child_count++;
    if (index < child_count) {
        RegNode* child = key->first_child;
        while (index--) child = child->next_sibling;
        memset(out, 0, sizeof(*out));
        strcpy(out->name, child->name);
        out->is_key = 1;
        strcpy(out->text, "Key");
        return 0;
    }
    index -= child_count;
    RegValue* value = key->first_value;
    while (value && index--) value = value->next;
    if (!value) return -1;
    memset(out, 0, sizeof(*out));
    strcpy(out->name, value->name);
    out->type = value->type;
    if (value->type == REG_TYPE_STRING && value->data) {
        uint32_t i = 0;
        while (i + 1 < sizeof(out->text) && i < value->data_size && ((char*)value->data)[i]) {
            out->text[i] = ((char*)value->data)[i];
            i++;
        }
        out->text[i] = '\0';
    } else if ((value->type == REG_TYPE_UINT32 || value->type == REG_TYPE_HEX) &&
               value->data && value->data_size >= sizeof(uint32_t)) {
        append_u32(out->text, *(uint32_t*)value->data);
    } else {
        strcpy(out->text, "<binary>");
    }
    return 0;
}
static RegNode* node_alloc(const char* name) {
    RegNode* n = (RegNode*)kzalloc(sizeof(RegNode));
    if (!n) return NULL;
    int i = 0;
    while (name[i] && i < HIVE_KEY_NAME_MAX - 1) { n->name[i] = name[i]; i++; }
    n->name[i] = '\0';
    return n;
}

static RegValue* value_alloc(const char* name, uint8_t type, void* data, uint32_t size) {
    RegValue* v = (RegValue*)kzalloc(sizeof(RegValue));
    if (!v) return NULL;
    int i = 0;
    while (name[i] && i < HIVE_VAL_NAME_MAX - 1) { v->name[i] = name[i]; i++; }
    v->name[i] = '\0';
    v->type = type;
    v->data = data;
    v->data_size = size;
    return v;
}
int regfs_load_hive(RegFS* rfs, const uint8_t* data, uint32_t size) {
    if (!rfs || !data || size < sizeof(HiveHeader)) return -1;

    const HiveHeader* hdr = (const HiveHeader*)data;
    if (hdr->magic != HIVE_MAGIC) {
        serial_emergency_print("regfs: bad magic\n");
        return -1;
    }
    if (hdr->version != HIVE_VERSION) {
        serial_emergency_print("regfs: unsupported version\n");
        return -1;
    }
    uint64_t key_bytes = (uint64_t)hdr->key_count * sizeof(HiveKey);
    uint64_t value_bytes = (uint64_t)hdr->val_count * sizeof(HiveValue);
    uint64_t expected = sizeof(HiveHeader) + key_bytes + value_bytes + hdr->data_size;
    if (!hdr->key_count || expected > size ||
        key_bytes > size - sizeof(HiveHeader) ||
        value_bytes > size - sizeof(HiveHeader) - key_bytes ||
        hdr->data_size > size - sizeof(HiveHeader) - key_bytes - value_bytes) {
        serial_emergency_print("regfs: size mismatch\n");
        return -1;
    }

    const HiveKey*   keys = (const HiveKey*)(data + sizeof(HiveHeader));
    const HiveValue* vals = (const HiveValue*)(keys + hdr->key_count);
    const uint8_t*   blob = (const uint8_t*)(vals + hdr->val_count);
    RegNode** nodes = (RegNode**)kzalloc(sizeof(RegNode*) * hdr->key_count);
    if (!nodes) return -1;

    for (uint32_t i = 0; i < hdr->key_count; i++) {
        nodes[i] = node_alloc(keys[i].name);
        if (!nodes[i]) { /* cleanup */ return -1; }
        nodes[i]->flags = keys[i].flags;
    }
    for (uint32_t i = 0; i < hdr->key_count; i++) {
        int32_t pid = keys[i].parent_idx;
        if (i == 0) {
            if (pid != -1) { kfree(nodes); return -1; }
            continue;
        }
        if (pid < 0 || (uint32_t)pid >= i) { kfree(nodes); return -1; }
        RegNode* parent = nodes[pid];
        RegNode* child = nodes[i];
        child->parent = parent;
        if (!parent->first_child) {
            parent->first_child = child;
        } else {
            RegNode* cur = parent->first_child;
            while (cur->next_sibling) cur = cur->next_sibling;
            cur->next_sibling = child;
        }
    }
    for (uint32_t i = 0; i < hdr->key_count; i++) {
        uint32_t first = keys[i].first_val_idx;
        uint32_t count = keys[i].val_count;
        if (first > hdr->val_count || count > hdr->val_count - first) {
            kfree(nodes);
            return -1;
        }
        for (uint32_t j = 0; j < count; j++) {
            uint32_t vi = first + j;
            const HiveValue* hv = &vals[vi];
            void* vdata = NULL;
            if (hv->data_offset > hdr->data_size ||
                hv->data_size > hdr->data_size - hv->data_offset) {
                kfree(nodes);
                return -1;
            }
            if (hv->data_size) {
                vdata = kmalloc(hv->data_size);
                if (!vdata) { kfree(nodes); return -1; }
                memcpy(vdata, blob + hv->data_offset, hv->data_size);
            }
            RegValue* v = value_alloc(hv->name, hv->type, vdata, hv->data_size);
            if (!v) { if (vdata) kfree(vdata); kfree(nodes); return -1; }
            v->flags = hv->flags;
            if (!nodes[i]->first_value) {
                nodes[i]->first_value = v;
            } else {
                RegValue* cur = nodes[i]->first_value;
                while (cur->next) cur = cur->next;
                cur->next = v;
            }
        }
    }

    rfs->root = nodes[0];
    kfree(nodes);

    serial_emergency_print("regfs: loaded hive, keys=");
    serial_emergency_print_hex(hdr->key_count);
    serial_emergency_print(" values=");
    serial_emergency_print_hex(hdr->val_count);
    serial_emergency_print("\n");
    return 0;
}
static uint32_t count_nodes(RegNode* n) {
    if (!n) return 0;
    uint32_t c = 1;
    for (RegNode* ch = n->first_child; ch; ch = ch->next_sibling) c += count_nodes(ch);
    return c;
}

static uint32_t count_values(RegNode* n) {
    if (!n) return 0;
    uint32_t c = 0;
    for (RegValue* v = n->first_value; v; v = v->next) c++;
    for (RegNode* ch = n->first_child; ch; ch = ch->next_sibling) c += count_values(ch);
    return c;
}

static uint32_t count_value_data(RegNode* node) {
    if (!node) return 0;
    uint32_t total = 0;
    for (RegValue* value = node->first_value; value; value = value->next) {
        if (0xFFFFFFFFu - total < value->data_size) return 0xFFFFFFFFu;
        total += value->data_size;
    }
    for (RegNode* child = node->first_child; child; child = child->next_sibling) {
        uint32_t child_size = count_value_data(child);
        if (0xFFFFFFFFu - total < child_size) return 0xFFFFFFFFu;
        total += child_size;
    }
    return total;
}
static uint32_t serialize_node(RegNode* n, int32_t parent_idx, uint32_t* key_idx,
                               HiveKey* keys, HiveValue* vals, uint8_t* blob,
                               uint32_t* val_idx, uint32_t* data_off) {
    uint32_t my_idx = (*key_idx)++;
    int i = 0;
    while (n->name[i] && i < HIVE_KEY_NAME_MAX - 1) { keys[my_idx].name[i] = n->name[i]; i++; }
    keys[my_idx].name[i] = '\0';
    keys[my_idx].parent_idx = parent_idx;
    keys[my_idx].first_val_idx = *val_idx;
    keys[my_idx].flags = n->flags;
    uint32_t vcount = 0;
    for (RegValue* v = n->first_value; v; v = v->next) {
        uint32_t vi = (*val_idx)++;
        i = 0;
        while (v->name[i] && i < HIVE_VAL_NAME_MAX - 1) { vals[vi].name[i] = v->name[i]; i++; }
        vals[vi].name[i] = '\0';
        vals[vi].type = v->type;
        vals[vi].flags = v->flags;
        vals[vi].data_offset = *data_off;
        vals[vi].data_size = v->data_size;
        if (v->data && v->data_size > 0) {
            memcpy(blob + *data_off, v->data, v->data_size);
            *data_off += v->data_size;
        }
        vcount++;
    }
    keys[my_idx].val_count = vcount;
    for (RegNode* ch = n->first_child; ch; ch = ch->next_sibling) {
        serialize_node(ch, my_idx, key_idx, keys, vals, blob, val_idx, data_off);
    }

    return *data_off;
}

int regfs_save_hive(RegFS* rfs, uint8_t** out_data, uint32_t* out_size) {
    if (!rfs->root) return -1;

    uint32_t nkeys = count_nodes(rfs->root);
    uint32_t nvals = count_values(rfs->root);
    uint32_t total_data = count_value_data(rfs->root);
    if (total_data == 0xFFFFFFFFu) return -1;

    uint32_t total = sizeof(HiveHeader) + nkeys * sizeof(HiveKey) +
                     nvals * sizeof(HiveValue) + total_data;
    uint8_t* buf = (uint8_t*)kzalloc(total);
    if (!buf) return -1;

    HiveHeader* hdr = (HiveHeader*)buf;
    hdr->magic = HIVE_MAGIC;
    hdr->version = HIVE_VERSION;
    hdr->key_count = nkeys;
    hdr->val_count = nvals;
    hdr->data_size = total_data;

    HiveKey*   keys = (HiveKey*)(buf + sizeof(HiveHeader));
    HiveValue* vals = (HiveValue*)(keys + nkeys);
    uint8_t*   blob = (uint8_t*)(vals + nvals);

    uint32_t key_idx = 0, val_idx = 0, data_off = 0;
    serialize_node(rfs->root, -1, &key_idx, keys, vals, blob, &val_idx, &data_off);

    *out_data = buf;
    *out_size = total;
    return 0;
}

static uint32_t text_append(char* buf, uint32_t cap, uint32_t pos, const char* s) {
    while (*s && pos < cap - 1) buf[pos++] = *s++;
    return pos;
}

static uint32_t text_append_hex(char* buf, uint32_t cap, uint32_t pos, uint32_t v) {
    const char* hx = "0123456789ABCDEF";
    char tmp[12];
    int n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    else { while (v) { tmp[n++] = hx[v & 0xF]; v >>= 4; } }
    for (int i = n - 1; i >= 0; i--) {
        if (pos < cap - 1) buf[pos++] = tmp[i];
    }
    return pos;
}

static uint32_t decompile_node(RegNode* n, char* buf, uint32_t cap, uint32_t pos,
                               char* path, uint32_t path_len) {
    if (pos < cap - 1) buf[pos++] = '[';
    if (path_len > 0) {
        for (uint32_t i = 0; i < path_len && pos < cap - 1; i++) buf[pos++] = path[i];
    }
    if (path_len > 0 && pos < cap - 1) buf[pos++] = '.';
    for (int i = 0; n->name[i] && pos < cap - 1; i++) buf[pos++] = n->name[i];

    if (pos < cap - 1) buf[pos++] = ']';
    if (pos < cap - 1) buf[pos++] = '\n';
    for (RegValue* v = n->first_value; v; v = v->next) {
        if (v->flags & 1) continue;

        for (int i = 0; v->name[i] && pos < cap - 1; i++) buf[pos++] = v->name[i];
        if (pos < cap - 1) buf[pos++] = ' ';
        if (pos < cap - 1) buf[pos++] = '=';
        if (pos < cap - 1) buf[pos++] = ' ';
        if (pos < cap - 1) buf[pos++] = ' ';

        switch (v->type) {
            case REG_TYPE_UINT32:
                if (v->data && v->data_size >= 4) {
                    uint32_t u = *(uint32_t*)v->data;
                    char tmp[16]; int n = 0;
                    if (u == 0) tmp[n++] = '0';
                    while (u) { tmp[n++] = '0' + (u % 10); u /= 10; }
                    while (n--) { if (pos < cap - 1) buf[pos++] = tmp[n]; }
                }
                break;
            case REG_TYPE_HEX:
                if (v->data && v->data_size >= 4) {
                    uint32_t u = *(uint32_t*)v->data;
                    pos = text_append(buf, cap, pos, "0x");
                    pos = text_append_hex(buf, cap, pos, u);
                }
                break;
            case REG_TYPE_UINT64:
                if (v->data && v->data_size >= 8) {
                    uint64_t u = *(uint64_t*)v->data;
                    pos = text_append_hex(buf, cap, pos, (uint32_t)(u >> 32));
                    pos = text_append_hex(buf, cap, pos, (uint32_t)u);
                }
                break;
            case REG_TYPE_STRING:
                if (v->data) {
                    if (pos < cap - 1) buf[pos++] = '"';
                    for (uint32_t i = 0; i < v->data_size && ((char*)v->data)[i]; i++) {
                        if (pos < cap - 1) buf[pos++] = ((char*)v->data)[i];
                    }
                    if (pos < cap - 1) buf[pos++] = '"';
                }
                break;
            case REG_TYPE_BINARY:
                if (v->data) {
                    if (pos < cap - 1) buf[pos++] = '<';
                    for (uint32_t i = 0; i < v->data_size; i++) {
                        pos = text_append_hex(buf, cap, pos, ((uint8_t*)v->data)[i]);
                    }
                    if (pos < cap - 1) buf[pos++] = '>';
                }
                break;
        }
        if (pos < cap - 1) buf[pos++] = '\n';
    }
    if (pos < cap - 1) buf[pos++] = '\n';
    char child_path[256];
    uint32_t cp = 0;
    for (uint32_t i = 0; i < path_len && cp < 255; i++) child_path[cp++] = path[i];
    if (path_len > 0 && cp < 255) child_path[cp++] = '.';
    for (int i = 0; n->name[i] && cp < 255; i++) child_path[cp++] = n->name[i];
    child_path[cp] = '\0';

    for (RegNode* ch = n->first_child; ch; ch = ch->next_sibling) {
        pos = decompile_node(ch, buf, cap, pos, child_path, cp);
    }

    return pos;
}

int regfs_decompile(RegFS* rfs, char** out_text, uint32_t* out_size) {
    if (!rfs->root) return -1;

    uint32_t cap = 64 * 1024;
    char* buf = (char*)kmalloc(cap);
    if (!buf) return -1;

    uint32_t pos = 0;
    for (RegNode* ch = rfs->root->first_child; ch; ch = ch->next_sibling) {
        pos = decompile_node(ch, buf, cap, pos, "", 0);
    }

    buf[pos] = '\0';
    *out_text = buf;
    *out_size = pos;
    return 0;
}
void regfs_unlock_easter_eggs(RegFS* rfs) {
    if (!rfs->root) return;
    RegNode* system = NULL;
    for (RegNode* ch = rfs->root->first_child; ch; ch = ch->next_sibling) {
        if (strcmp(ch->name, "SYSTEM") == 0) { system = ch; break; }
    }
    if (!system) return;
    RegNode* eggs = NULL;
    for (RegNode* ch = system->first_child; ch; ch = ch->next_sibling) {
        if (strcmp(ch->name, "easter_eggs") == 0) { eggs = ch; break; }
    }
    if (!eggs) {
        eggs = node_alloc("easter_eggs");
        eggs->parent = system;
        eggs->next_sibling = system->first_child;
        system->first_child = eggs;
    }
    eggs->flags = 0;
    const char* names[] = { "enabled", "boot_message", "nyan_cat",
                            "matrix_rain", "rickroll", "hidden_cmd" };
    uint8_t types[] = { REG_TYPE_HEX, REG_TYPE_STRING, REG_TYPE_HEX,
                        REG_TYPE_HEX, REG_TYPE_HEX, REG_TYPE_STRING };
    for (int i = 0; i < 6; i++) {
        int exists = 0;
        for (RegValue* v = eggs->first_value; v; v = v->next) {
            if (strcmp(v->name, names[i]) == 0) { exists = 1; break; }
        }
        if (exists) continue;

        if (types[i] == REG_TYPE_HEX) {
            uint32_t* d = (uint32_t*)kmalloc(4);
            *d = 0;
            RegValue* v = value_alloc(names[i], REG_TYPE_HEX, d, 4);
            if (!eggs->first_value) eggs->first_value = v;
            else { RegValue* cur = eggs->first_value; while (cur->next) cur = cur->next; cur->next = v; }
        } else {
            char* d = (char*)kmalloc(1);
            d[0] = '\0';
            RegValue* v = value_alloc(names[i], REG_TYPE_STRING, d, 1);
            if (!eggs->first_value) eggs->first_value = v;
            else { RegValue* cur = eggs->first_value; while (cur->next) cur = cur->next; cur->next = v; }
        }
    }
    rfs->dirty = 1;
    serial_emergency_print("regfs: easter eggs unlocked\n");
}