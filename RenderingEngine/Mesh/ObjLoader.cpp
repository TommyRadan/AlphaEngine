#include <RenderingEngine/Mesh/ObjLoader.hpp>

#include <Infrastructure/Exception.hpp>
#include <Mathematics/glm.hpp>

#include <fstream>

enum mode
{
    MODE_NONE,
    MODE_UNKNOWN,
    MODE_VERTEX,
    MODE_NORMAL,
    MODE_TEXTURE,
    MODE_FACE
};

struct face
{
    int v1, v2, v3;
    int t1, t2, t3;
    int n1, n2, n3;
};

static inline bool IS_SPACING(char c)
{
    return c == ' ' || c == '\n' || c == '\t';
}

static inline int readInt(char* buf, int& out)
{
    // Determine length and sign of number
    int l = 0;
    bool neg = false;
    if (buf[0] == '-') { neg = true; buf++; }
    while (buf[l] >= '0' && buf[l] <= '9') l++;

    // Multiply each digit by the appropriate power of 10
    int n = 0;
    int p = 1;
    for (int i = l - 1; i >= 0; i--) {
        n += (buf[i] - '0') * p;
        p *= 10;
    }

    // Handle sign
    if (neg) {
        out = -n;
        l++;
    }
    else
        out = n;

    return l;
}

static inline int readFloat(char* buf, float& out)
{
    // Read natural part
    bool neg = buf[0] == '-';
    int nat, frac;
    int ln = readInt(buf, nat);

    // Read fractional part
    buf += ln + 1;
    int lf = readInt(buf, frac);
    int p = 1;
    for (int i = 0; i < lf; i++) p *= 10;

    // Handle sign
    if (neg)
        out = nat - frac / (float)p;
    else
        out = nat + frac / (float)p;

    return ln + lf + 1;
}

static int readVector(char* buf, glm::vec3& out)
{
    int i = 0;
    while (IS_SPACING(*buf)) { buf++; i++; };
    buf += readFloat(buf, out.x);
    while (IS_SPACING(*buf)) { buf++; i++; }
    buf += readFloat(buf, out.y);
    while (IS_SPACING(*buf)) { buf++; i++; }
    buf += readFloat(buf, out.z);
    return i;
}

int readFace(char* buf, face& out)
{
    int i = 0;
    while (IS_SPACING(*buf)) { buf++; i++; };
    buf += readInt(buf, out.v1) + 1;
    buf += readInt(buf, out.t1) + 1;
    buf += readInt(buf, out.n1) + 1;
    while (IS_SPACING(*buf)) { buf++; i++; }
    buf += readInt(buf, out.v2) + 1;
    buf += readInt(buf, out.t2) + 1;
    buf += readInt(buf, out.n2) + 1;
    while (IS_SPACING(*buf)) { buf++; i++; }
    buf += readInt(buf, out.v3) + 1;
    buf += readInt(buf, out.t3) + 1;
    buf += readInt(buf, out.n3) + 1;
    return i;
}

std::vector<RenderingEngine::Vertex> RenderingEngine::LoadOBJ(const std::string& filename)
{
    std::vector <Vertex> vertices;

    // Read file into memory
    std::ifstream file(filename.c_str(), std::ios::in | std::ios::ate);
    if (!file.is_open()) {
        throw Exception("Mesh loading failed (" + filename + ")");
    }

    auto fileLen = (unsigned int) file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buf(fileLen);
    file.read((char *) &buf[0], fileLen);
    file.close();

    // Data
    int approxMem = 295 + fileLen / 1024 * 11;
    std::vector <glm::vec3> vectors;
    vectors.reserve(approxMem);

    std::vector <glm::vec3> normals;
    normals.reserve(approxMem);

    std::vector <glm::vec2> texcoords;
    texcoords.reserve(approxMem);

    vertices.reserve(approxMem);

    // Parser state
    bool inComment = false;
    int mode = MODE_NONE;
    glm::vec3 v3;
    face f;

    bool hasNormals = false;
    bool hasTexcoords = false;

    // Parse
    for (unsigned int i = 0; i < fileLen; i++) {
        // Ignore comment lines
        if (buf[i] == '#') {
            inComment = true;
            continue;
        } else if (inComment && buf[i] != '\n') {
            continue;
        } else if (inComment && buf[i] == '\n') {
            inComment = false;
            continue;
        }

        // Find a command
        if (mode == MODE_NONE && !IS_SPACING((char) buf[i])) {
            if (buf[i] == 'f')
                mode = MODE_FACE;
            else if (buf[i] == 'v') {
                if (IS_SPACING((char) buf[i + 1]))
                    mode = MODE_VERTEX;
                else if (buf[i + 1] == 'n')
                    mode = MODE_NORMAL;
                else if (buf[i + 1] == 't')
                    mode = MODE_TEXTURE;
                i++;
            } else
                mode = MODE_UNKNOWN;
        } else if (mode != MODE_NONE) {
            switch (mode) {
                case MODE_UNKNOWN:
                    if (buf[i] == '\n')
                        mode = MODE_NONE;
                    break;

                case MODE_VERTEX:
                    i += readVector((char *) &buf[i], v3);
                    vectors.push_back(v3);
                    mode = MODE_NONE;
                    break;

                case MODE_NORMAL:
                    i += readVector((char *) &buf[i], v3);
                    normals.push_back(v3);
                    mode = MODE_NONE;
                    hasNormals = true;
                    break;

                case MODE_TEXTURE:
                    i += readVector((char *) &buf[i], v3);
                    texcoords.push_back(glm::vec2(v3.x, v3.y));
                    mode = MODE_NONE;
                    hasTexcoords = true;
                    break;

                case MODE_FACE:
                    i += readFace((char *) &buf[i], f);

                    Vertex vertex = {
                            vectors[f.v1 - 1],
                            hasTexcoords ? texcoords[f.t1 - 1] : glm::vec2(0.0f, 0.0f),
                            hasNormals ? normals[f.n1 - 1] : glm::vec3(0.0f, 0.0f, 0.0f)
                    };
                    vertices.push_back(vertex);

                    vertex.Pos = vectors[f.v2 - 1];
                    vertex.Tex = (hasTexcoords ? texcoords[f.t2 - 1] : glm::vec2(0.0f, 0.0f));
                    vertex.Normal = (hasNormals ? normals[f.n2 - 1] : glm::vec3(0.0f, 0.0f, 0.0f));
                    vertices.push_back(vertex);

                    vertex.Pos = vectors[f.v3 - 1];
                    vertex.Tex = (hasTexcoords ? texcoords[f.t3 - 1] : glm::vec2(0.0f, 0.0f));
                    vertex.Normal = (hasNormals ? normals[f.n3 - 1] : glm::vec3(0.0f, 0.0f, 0.0f));
                    vertices.push_back(vertex);

                    mode = MODE_NONE;
                    break;
            }
        }
    }

    return vertices;
}
