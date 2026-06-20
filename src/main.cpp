#include <gtk/gtk.h>
#include <pango/pango.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static const double PI = 3.14159265358979323846;

static std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static bool file_exists(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

static void ensure_dir(const std::string& p) {
    mkdir(p.c_str(), 0755);
}

static std::string extension_root() {
    if (file_exists("/mnt/us/extensions/kindlesettlers")) return "/mnt/us/extensions/kindlesettlers";
    return ".";
}

static std::string save_path() { return extension_root() + "/data/save.json"; }
static std::string settings_path() { return extension_root() + "/data/settings.json"; }
static std::string app_log_path() { return extension_root() + "/data/kindlesettlers.log"; }

static void append_app_log(const std::string& line) {
    ensure_dir(extension_root() + "/data");
    std::ofstream f(app_log_path(), std::ios::app);
    if (f) f << line << "\n";
}

static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"') { out.push_back('\\'); out.push_back(c); }
        else if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

struct Pt { double x = 0; double y = 0; };
struct Rect { int x = 0, y = 0, w = 0, h = 0; bool contains(int px, int py) const { return px >= x && py >= y && px <= x + w && py <= y + h; } };

enum class Resource { Wood = 0, Brick = 1, Sheep = 2, Wheat = 3, Ore = 4, Desert = 5 };
enum class BuildType { None = 0, Road = 1, Settlement = 2, City = 3 };
enum class Screen { Menu, Setup, Handoff, Game, BuildMenu, SelectRoad, SelectSettlement, SelectCity, ConfirmBuild, RollResult, MoveBandit, Log, Rules, Settings, GameOver };

enum class Action {
    None, Resume, NewGame, Rules, Exit, Back, SetupPlayers2, SetupPlayers3, SetupPlayers4, StartGame,
    ConfirmHandoff, RollDice, Build, EndTurn, Log, Menu, BuildRoad, BuildSettlement, BuildCity,
    ConfirmBuild, CancelBuild, ViewBoard, Settings, SaveGame, TextMinus, TextPlus
};

static const char* resource_name(Resource r) {
    switch (r) {
        case Resource::Wood: return "Wood";
        case Resource::Brick: return "Brick";
        case Resource::Sheep: return "Sheep";
        case Resource::Wheat: return "Wheat";
        case Resource::Ore: return "Ore";
        case Resource::Desert: return "Desert";
    }
    return "?";
}

static Resource resource_from_int(int v) {
    if (v < 0 || v > 5) return Resource::Desert;
    return static_cast<Resource>(v);
}

struct Hex {
    int id = -1;
    int q = 0;
    int r = 0;
    Resource resource = Resource::Desert;
    int number = 0;
    bool bandit = false;
    std::array<int, 6> vertices{};
    std::array<int, 6> edges{};
};

struct Vertex {
    int id = -1;
    Pt p;
    std::vector<int> adjacent_hexes;
    std::vector<int> adjacent_edges;
    int owner = -1;
    BuildType building = BuildType::None;
};

struct Edge {
    int id = -1;
    int a = -1;
    int b = -1;
    std::vector<int> adjacent_hexes;
    int owner = -1;
};

struct Player {
    std::array<int, 5> res{};
    int progress_count = 0;
    int hidden_points = 0;
    int guards_played = 0;
};

struct Board {
    std::vector<Hex> hexes;
    std::vector<Vertex> vertices;
    std::vector<Edge> edges;

    void build_topology() {
        hexes.clear(); vertices.clear(); edges.clear();
        int id = 0;
        for (int q = -2; q <= 2; ++q) {
            int r1 = std::max(-2, -q - 2);
            int r2 = std::min(2, -q + 2);
            for (int r = r1; r <= r2; ++r) {
                Hex h;
                h.id = id++;
                h.q = q; h.r = r;
                hexes.push_back(h);
            }
        }
        std::map<std::string, int> vertex_key;
        std::map<std::string, int> edge_key;
        for (auto& h : hexes) {
            Pt c = axial_to_unit(h.q, h.r);
            std::array<int, 6> vids{};
            for (int i = 0; i < 6; ++i) {
                double ang = (30.0 + 60.0 * i) * PI / 180.0;
                Pt p{c.x + std::cos(ang), c.y + std::sin(ang)};
                std::string k = coord_key(p);
                auto it = vertex_key.find(k);
                if (it == vertex_key.end()) {
                    Vertex v;
                    v.id = (int)vertices.size();
                    v.p = p;
                    vertices.push_back(v);
                    vertex_key[k] = v.id;
                    vids[i] = v.id;
                } else {
                    vids[i] = it->second;
                }
                vertices[vids[i]].adjacent_hexes.push_back(h.id);
            }
            for (int i = 0; i < 6; ++i) {
                int a = vids[i];
                int b = vids[(i + 1) % 6];
                int lo = std::min(a, b), hi = std::max(a, b);
                std::string ek = std::to_string(lo) + ":" + std::to_string(hi);
                auto eit = edge_key.find(ek);
                int eid;
                if (eit == edge_key.end()) {
                    Edge e;
                    e.id = (int)edges.size();
                    e.a = lo; e.b = hi;
                    edges.push_back(e);
                    edge_key[ek] = e.id;
                    eid = e.id;
                    vertices[a].adjacent_edges.push_back(eid);
                    vertices[b].adjacent_edges.push_back(eid);
                } else {
                    eid = eit->second;
                }
                edges[eid].adjacent_hexes.push_back(h.id);
                h.vertices[i] = vids[i];
                h.edges[i] = eid;
            }
        }
        for (auto& v : vertices) {
            sort_unique(v.adjacent_hexes);
            sort_unique(v.adjacent_edges);
        }
        for (auto& e : edges) sort_unique(e.adjacent_hexes);
    }

    static void sort_unique(std::vector<int>& xs) {
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    }

    static Pt axial_to_unit(int q, int r) {
        return Pt{std::sqrt(3.0) * (q + r / 2.0), 1.5 * r};
    }

    static std::string coord_key(const Pt& p) {
        long long x = llround(p.x * 10000.0);
        long long y = llround(p.y * 10000.0);
        return std::to_string(x) + "," + std::to_string(y);
    }

    int current_bandit_hex() const {
        for (const auto& h : hexes) if (h.bandit) return h.id;
        return -1;
    }
};

struct GameState {
    int version = 1;
    int player_count = 2;
    int current_player = 0;
    bool active = false;
    bool initial_phase = false;
    int initial_index = 0;
    bool placing_settlement = true;
    int last_placed_vertex = -1;
    bool dice_rolled = false;
    int last_roll = 0;
    int random_seed = 0;
    int longest_route_holder = -1;
    int longest_route_length = 0;
    int largest_patrol_holder = -1;
    int largest_patrol_count = 0;
    std::vector<Player> players;
    Board board;
    std::vector<std::string> log;

    void reset(int pc) {
        player_count = pc;
        current_player = 0;
        active = true;
        initial_phase = true;
        initial_index = 0;
        placing_settlement = true;
        last_placed_vertex = -1;
        dice_rolled = false;
        last_roll = 0;
        longest_route_holder = -1;
        longest_route_length = 0;
        largest_patrol_holder = -1;
        largest_patrol_count = 0;
        players.assign(player_count, Player{});
        log.clear();
        random_seed = (int)std::time(nullptr);
        board.build_topology();
        randomize_board(random_seed);
        add_log("New game started with " + std::to_string(player_count) + " players.");
    }

    int visible_points(int p) const {
        int pts = 0;
        for (const auto& v : board.vertices) {
            if (v.owner == p) pts += (v.building == BuildType::City) ? 2 : (v.building == BuildType::Settlement ? 1 : 0);
        }
        if (longest_route_holder == p) pts += 2;
        if (largest_patrol_holder == p) pts += 2;
        return pts;
    }

    int total_points(int p) const { return visible_points(p) + players[p].hidden_points; }

    int resource_count(int p) const {
        int n = 0;
        for (int v : players[p].res) n += v;
        return n;
    }

    int current_setup_player() const {
        if (initial_index < player_count) return initial_index;
        return player_count - 1 - (initial_index - player_count);
    }

    bool can_afford(int p, BuildType t) const {
        if (initial_phase) return true;
        const auto& r = players[p].res;
        if (t == BuildType::Road) return r[0] >= 1 && r[1] >= 1;
        if (t == BuildType::Settlement) return r[0] >= 1 && r[1] >= 1 && r[2] >= 1 && r[3] >= 1;
        if (t == BuildType::City) return r[3] >= 2 && r[4] >= 3;
        return false;
    }

    void pay_cost(int p, BuildType t) {
        if (initial_phase) return;
        auto& r = players[p].res;
        if (t == BuildType::Road) { r[0]--; r[1]--; }
        if (t == BuildType::Settlement) { r[0]--; r[1]--; r[2]--; r[3]--; }
        if (t == BuildType::City) { r[3] -= 2; r[4] -= 3; }
    }

    bool settlement_distance_ok(int vertex_id) const {
        if (vertex_id < 0 || vertex_id >= (int)board.vertices.size()) return false;
        const auto& v = board.vertices[vertex_id];
        if (v.owner >= 0) return false;
        for (int eid : v.adjacent_edges) {
            const auto& e = board.edges[eid];
            int other = (e.a == vertex_id) ? e.b : e.a;
            if (board.vertices[other].owner >= 0) return false;
        }
        return true;
    }

    bool valid_settlement(int vertex_id, int p) const {
        if (!settlement_distance_ok(vertex_id)) return false;
        if (initial_phase) return true;
        for (int eid : board.vertices[vertex_id].adjacent_edges) {
            if (board.edges[eid].owner == p) return true;
        }
        return false;
    }

    bool edge_connected_to_player(int edge_id, int p) const {
        const auto& e = board.edges[edge_id];
        std::array<int, 2> ends{e.a, e.b};
        for (int v_id : ends) {
            const auto& v = board.vertices[v_id];
            if (v.owner == p) return true;
            if (v.owner >= 0 && v.owner != p) continue;
            for (int adj_eid : v.adjacent_edges) {
                if (adj_eid != edge_id && board.edges[adj_eid].owner == p) return true;
            }
        }
        return false;
    }

    bool valid_road(int edge_id, int p) const {
        if (edge_id < 0 || edge_id >= (int)board.edges.size()) return false;
        if (board.edges[edge_id].owner >= 0) return false;
        if (initial_phase) {
            if (last_placed_vertex < 0) return false;
            return board.edges[edge_id].a == last_placed_vertex || board.edges[edge_id].b == last_placed_vertex;
        }
        return edge_connected_to_player(edge_id, p);
    }

    bool valid_city(int vertex_id, int p) const {
        if (vertex_id < 0 || vertex_id >= (int)board.vertices.size()) return false;
        const auto& v = board.vertices[vertex_id];
        return v.owner == p && v.building == BuildType::Settlement;
    }

    void build_settlement(int vertex_id, int p) {
        pay_cost(p, BuildType::Settlement);
        board.vertices[vertex_id].owner = p;
        board.vertices[vertex_id].building = BuildType::Settlement;
        last_placed_vertex = vertex_id;
        add_log("P" + std::to_string(p + 1) + " built a settlement.");
        if (initial_phase && initial_index >= player_count) grant_initial_resources(vertex_id, p);
    }

    void build_city(int vertex_id, int p) {
        pay_cost(p, BuildType::City);
        board.vertices[vertex_id].building = BuildType::City;
        add_log("P" + std::to_string(p + 1) + " upgraded to a city.");
    }

    void build_road(int edge_id, int p) {
        pay_cost(p, BuildType::Road);
        board.edges[edge_id].owner = p;
        add_log("P" + std::to_string(p + 1) + " built a road.");
        update_longest_route();
    }

    void grant_initial_resources(int vertex_id, int p) {
        std::array<int, 5> gained{};
        for (int hid : board.vertices[vertex_id].adjacent_hexes) {
            Resource r = board.hexes[hid].resource;
            if (r != Resource::Desert) {
                players[p].res[(int)r]++;
                gained[(int)r]++;
            }
        }
        std::string msg = "P" + std::to_string(p + 1) + " gained starting resources";
        bool any = false;
        for (int i = 0; i < 5; ++i) if (gained[i]) { msg += any ? ", " : ": "; any = true; msg += std::string(resource_name((Resource)i)) + " " + std::to_string(gained[i]); }
        if (!any) msg += ": none";
        add_log(msg + ".");
    }

    void advance_initial() {
        if (!initial_phase) return;
        if (placing_settlement) {
            placing_settlement = false;
        } else {
            placing_settlement = true;
            last_placed_vertex = -1;
            initial_index++;
            if (initial_index >= player_count * 2) {
                initial_phase = false;
                current_player = 0;
                dice_rolled = false;
                add_log("Initial placement complete.");
            } else {
                current_player = current_setup_player();
            }
        }
    }

    void roll_dice(std::mt19937& rng) {
        std::uniform_int_distribution<int> d(1, 6);
        int a = d(rng), b = d(rng);
        last_roll = a + b;
        dice_rolled = true;
        add_log("P" + std::to_string(current_player + 1) + " rolled " + std::to_string(last_roll) + ".");
        if (last_roll == 7) {
            add_log("Bandit activated. Move the Bandit to a new hex.");
            return;
        }
        produce_resources(last_roll);
    }

    void produce_resources(int roll) {
        std::array<std::array<int, 5>, 4> gained{};
        for (const auto& h : board.hexes) {
            if (h.number != roll || h.bandit || h.resource == Resource::Desert) continue;
            for (int vid : h.vertices) {
                const auto& v = board.vertices[vid];
                if (v.owner < 0) continue;
                int amt = (v.building == BuildType::City) ? 2 : 1;
                gained[v.owner][(int)h.resource] += amt;
                players[v.owner].res[(int)h.resource] += amt;
            }
        }
        bool any = false;
        for (int p = 0; p < player_count; ++p) {
            for (int r = 0; r < 5; ++r) {
                if (gained[p][r] > 0) {
                    any = true;
                    add_log("P" + std::to_string(p + 1) + " gained " + std::to_string(gained[p][r]) + " " + resource_name((Resource)r) + ".");
                }
            }
        }
        if (!any) add_log("No resources produced.");
    }

    void move_bandit(int hex_id) {
        for (auto& h : board.hexes) h.bandit = false;
        if (hex_id >= 0 && hex_id < (int)board.hexes.size()) {
            board.hexes[hex_id].bandit = true;
            add_log("P" + std::to_string(current_player + 1) + " moved the Bandit.");
        }
    }

    void end_turn() {
        dice_rolled = false;
        last_roll = 0;
        current_player = (current_player + 1) % player_count;
        add_log("Turn passed to P" + std::to_string(current_player + 1) + ".");
    }

    void update_longest_route() {
        int best_player = longest_route_holder;
        int best_len = longest_route_length;
        for (int p = 0; p < player_count; ++p) {
            int len = longest_route_for_player(p);
            if (len >= 5 && len > best_len) {
                best_player = p;
                best_len = len;
            }
        }
        if (best_player != longest_route_holder && best_player >= 0) {
            longest_route_holder = best_player;
            longest_route_length = best_len;
            add_log("P" + std::to_string(best_player + 1) + " claimed Longest Route, length " + std::to_string(best_len) + ".");
        } else {
            longest_route_length = std::max(best_len, longest_route_length);
        }
    }

    int longest_route_for_player(int p) const {
        std::vector<int> road_edges;
        for (const auto& e : board.edges) if (e.owner == p) road_edges.push_back(e.id);
        int best = 0;
        std::set<int> used;
        for (int eid : road_edges) {
            const Edge& e = board.edges[eid];
            used.insert(eid);
            best = std::max(best, 1 + dfs_route(e.a, p, used));
            best = std::max(best, 1 + dfs_route(e.b, p, used));
            used.erase(eid);
        }
        return best;
    }

    int dfs_route(int vertex_id, int p, std::set<int>& used) const {
        const Vertex& v = board.vertices[vertex_id];
        if (v.owner >= 0 && v.owner != p) return 0;
        int best = 0;
        for (int eid : v.adjacent_edges) {
            const Edge& e = board.edges[eid];
            if (e.owner != p || used.count(eid)) continue;
            int next = (e.a == vertex_id) ? e.b : e.a;
            used.insert(eid);
            best = std::max(best, 1 + dfs_route(next, p, used));
            used.erase(eid);
        }
        return best;
    }

    void randomize_board(int seed) {
        std::vector<Resource> resources = {
            Resource::Wood, Resource::Wood, Resource::Wood, Resource::Wood,
            Resource::Sheep, Resource::Sheep, Resource::Sheep, Resource::Sheep,
            Resource::Wheat, Resource::Wheat, Resource::Wheat, Resource::Wheat,
            Resource::Brick, Resource::Brick, Resource::Brick,
            Resource::Ore, Resource::Ore, Resource::Ore,
            Resource::Desert
        };
        std::vector<int> nums = {2,3,3,4,4,5,5,6,6,8,8,9,9,10,10,11,11,12};
        std::mt19937 rng(seed);
        std::shuffle(resources.begin(), resources.end(), rng);
        std::shuffle(nums.begin(), nums.end(), rng);
        int ni = 0;
        for (size_t i = 0; i < board.hexes.size(); ++i) {
            board.hexes[i].resource = resources[i];
            board.hexes[i].bandit = resources[i] == Resource::Desert;
            board.hexes[i].number = (resources[i] == Resource::Desert) ? 0 : nums[ni++];
        }
    }

    void add_log(const std::string& s) {
        log.push_back(s);
        if (log.size() > 80) log.erase(log.begin());
    }

    std::string save_json() const {
        std::ostringstream o;
        o << "{\n";
        o << "  \"version\": " << version << ",\n";
        o << "  \"player_count\": " << player_count << ",\n";
        o << "  \"current_player\": " << current_player << ",\n";
        o << "  \"active\": " << (active ? 1 : 0) << ",\n";
        o << "  \"initial_phase\": " << (initial_phase ? 1 : 0) << ",\n";
        o << "  \"initial_index\": " << initial_index << ",\n";
        o << "  \"placing_settlement\": " << (placing_settlement ? 1 : 0) << ",\n";
        o << "  \"last_placed_vertex\": " << last_placed_vertex << ",\n";
        o << "  \"dice_rolled\": " << (dice_rolled ? 1 : 0) << ",\n";
        o << "  \"last_roll\": " << last_roll << ",\n";
        o << "  \"random_seed\": " << random_seed << ",\n";
        o << "  \"longest_route_holder\": " << longest_route_holder << ",\n";
        o << "  \"longest_route_length\": " << longest_route_length << ",\n";
        o << "  \"largest_patrol_holder\": " << largest_patrol_holder << ",\n";
        o << "  \"largest_patrol_count\": " << largest_patrol_count << ",\n";
        o << "  \"hexes\": [";
        for (size_t i = 0; i < board.hexes.size(); ++i) {
            const auto& h = board.hexes[i];
            if (i) o << ",";
            o << "{\"res\":" << (int)h.resource << ",\"num\":" << h.number << ",\"bandit\":" << (h.bandit ? 1 : 0) << "}";
        }
        o << "],\n  \"vertices\": [";
        for (size_t i = 0; i < board.vertices.size(); ++i) {
            const auto& v = board.vertices[i];
            if (i) o << ",";
            o << "{\"owner\":" << v.owner << ",\"building\":" << (int)v.building << "}";
        }
        o << "],\n  \"edges\": [";
        for (size_t i = 0; i < board.edges.size(); ++i) {
            const auto& e = board.edges[i];
            if (i) o << ",";
            o << "{\"owner\":" << e.owner << "}";
        }
        o << "],\n  \"players\": [";
        for (int p = 0; p < player_count; ++p) {
            if (p) o << ",";
            o << "{\"res\":[";
            for (int r = 0; r < 5; ++r) { if (r) o << ","; o << players[p].res[r]; }
            o << "],\"progress_count\":" << players[p].progress_count << ",\"hidden_points\":" << players[p].hidden_points << ",\"guards_played\":" << players[p].guards_played << "}";
        }
        o << "],\n  \"log\": [";
        for (size_t i = 0; i < log.size(); ++i) {
            if (i) o << ",";
            o << "\"" << json_escape(log[i]) << "\"";
        }
        o << "]\n}\n";
        return o.str();
    }

    void save() const {
        ensure_dir(extension_root() + "/data");
        std::ofstream f(save_path());
        f << save_json();
    }

    static int get_int(const std::string& s, const std::string& key, int def) {
        try {
            std::regex re("\\\"" + key + "\\\"\\s*:\\s*(-?\\d+)");
            std::smatch m;
            if (std::regex_search(s, m, re)) return std::stoi(m[1].str());
        } catch (...) {}
        return def;
    }

    static std::string get_array_block(const std::string& s, const std::string& key) {
        std::string needle = "\"" + key + "\"";
        size_t k = s.find(needle);
        if (k == std::string::npos) return "";
        size_t a = s.find('[', k);
        if (a == std::string::npos) return "";
        int depth = 0;
        for (size_t i = a; i < s.size(); ++i) {
            if (s[i] == '[') depth++;
            if (s[i] == ']') {
                depth--;
                if (depth == 0) return s.substr(a + 1, i - a - 1);
            }
        }
        return "";
    }

    static std::vector<std::string> object_blocks(const std::string& block) {
        std::vector<std::string> out;
        int depth = 0;
        size_t start = 0;
        for (size_t i = 0; i < block.size(); ++i) {
            if (block[i] == '{') { if (depth == 0) start = i; depth++; }
            if (block[i] == '}') { depth--; if (depth == 0) out.push_back(block.substr(start, i - start + 1)); }
        }
        return out;
    }

    bool load() {
        std::ifstream f(save_path());
        if (!f) return false;
        std::stringstream buf;
        buf << f.rdbuf();
        std::string s = buf.str();
        board.build_topology();
        version = get_int(s, "version", 1);
        player_count = std::max(2, std::min(4, get_int(s, "player_count", 2)));
        current_player = std::max(0, std::min(player_count - 1, get_int(s, "current_player", 0)));
        active = get_int(s, "active", 1) != 0;
        initial_phase = get_int(s, "initial_phase", 0) != 0;
        initial_index = get_int(s, "initial_index", 0);
        placing_settlement = get_int(s, "placing_settlement", 1) != 0;
        last_placed_vertex = get_int(s, "last_placed_vertex", -1);
        dice_rolled = get_int(s, "dice_rolled", 0) != 0;
        last_roll = get_int(s, "last_roll", 0);
        random_seed = get_int(s, "random_seed", (int)std::time(nullptr));
        longest_route_holder = get_int(s, "longest_route_holder", -1);
        longest_route_length = get_int(s, "longest_route_length", 0);
        largest_patrol_holder = get_int(s, "largest_patrol_holder", -1);
        largest_patrol_count = get_int(s, "largest_patrol_count", 0);
        players.assign(player_count, Player{});

        auto hex_objs = object_blocks(get_array_block(s, "hexes"));
        for (size_t i = 0; i < hex_objs.size() && i < board.hexes.size(); ++i) {
            board.hexes[i].resource = resource_from_int(get_int(hex_objs[i], "res", 5));
            board.hexes[i].number = get_int(hex_objs[i], "num", 0);
            board.hexes[i].bandit = get_int(hex_objs[i], "bandit", 0) != 0;
        }
        auto vertex_objs = object_blocks(get_array_block(s, "vertices"));
        for (size_t i = 0; i < vertex_objs.size() && i < board.vertices.size(); ++i) {
            board.vertices[i].owner = get_int(vertex_objs[i], "owner", -1);
            board.vertices[i].building = (BuildType)get_int(vertex_objs[i], "building", 0);
        }
        auto edge_objs = object_blocks(get_array_block(s, "edges"));
        for (size_t i = 0; i < edge_objs.size() && i < board.edges.size(); ++i) {
            board.edges[i].owner = get_int(edge_objs[i], "owner", -1);
        }
        auto player_objs = object_blocks(get_array_block(s, "players"));
        for (size_t p = 0; p < player_objs.size() && p < players.size(); ++p) {
            std::string rb = get_array_block(player_objs[p], "res");
            std::stringstream ss(rb);
            for (int r = 0; r < 5; ++r) {
                std::string part;
                if (!std::getline(ss, part, ',')) break;
                players[p].res[r] = std::max(0, atoi(part.c_str()));
            }
            players[p].progress_count = get_int(player_objs[p], "progress_count", 0);
            players[p].hidden_points = get_int(player_objs[p], "hidden_points", 0);
            players[p].guards_played = get_int(player_objs[p], "guards_played", 0);
        }
        log.clear();
        std::string lb = get_array_block(s, "log");
        std::regex lr("\"([^\"]*)\"");
        auto begin = std::sregex_iterator(lb.begin(), lb.end(), lr);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) log.push_back((*it)[1].str());
        if (log.empty()) add_log("Game resumed.");
        return true;
    }
};

struct Button { Rect r; std::string label; Action action = Action::None; bool enabled = true; };

struct LayoutCache {
    int w = 0, h = 0;
    Rect top, board, bottom;
    std::vector<Pt> hex_centers;
    std::vector<std::array<Pt,6>> hex_corners;
    std::vector<Pt> vertex_pts;
};

struct App {
    GtkWidget* window = nullptr;
    GtkWidget* area = nullptr;
    GameState gs;
    Screen screen = Screen::Menu;
    Screen previous_screen = Screen::Menu;
    int setup_players = 2;
    int text_size = 34;
    bool confirm_builds = true;
    bool handoff_privacy = true;
    int pending_edge = -1;
    int pending_vertex = -1;
    int pending_hex = -1;
    BuildType pending_type = BuildType::None;
    int handoff_player = 0;
    std::string message;
    std::vector<Button> buttons;
    LayoutCache layout;
    std::mt19937 rng;

    App() : rng((unsigned)std::time(nullptr)) {}

    void load_settings() {
        std::ifstream f(settings_path());
        if (!f) return;
        std::stringstream ss; ss << f.rdbuf();
        std::string s = ss.str();
        text_size = std::max(24, std::min(54, GameState::get_int(s, "text_size", text_size)));
        confirm_builds = GameState::get_int(s, "confirm_builds", 1) != 0;
        handoff_privacy = GameState::get_int(s, "handoff_privacy", 1) != 0;
    }

    void save_settings() const {
        ensure_dir(extension_root() + "/data");
        std::ofstream f(settings_path());
        f << "{\n  \"text_size\": " << text_size << ",\n";
        f << "  \"confirm_builds\": " << (confirm_builds ? 1 : 0) << ",\n";
        f << "  \"handoff_privacy\": " << (handoff_privacy ? 1 : 0) << "\n}\n";
    }

    void show(Screen s) { screen = s; redraw(); }
    void redraw() { if (area) gtk_widget_queue_draw(area); }

    void add_button(int x, int y, int w, int h, const std::string& label, Action action, bool enabled = true) {
        buttons.push_back(Button{Rect{x,y,w,h}, label, action, enabled});
    }

    void compute_layout(int w, int h) {
        layout.w = w; layout.h = h;
        layout.top = Rect{0, 0, w, std::max(76, h / 11)};
        layout.bottom = Rect{0, h - std::max(210, h / 4), w, std::max(210, h / 4)};
        layout.board = Rect{0, layout.top.h, w, h - layout.top.h - layout.bottom.h};
        layout.hex_centers.assign(gs.board.hexes.size(), Pt{});
        layout.hex_corners.assign(gs.board.hexes.size(), {});
        layout.vertex_pts.assign(gs.board.vertices.size(), Pt{});
        if (gs.board.hexes.empty()) return;
        double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
        std::vector<std::array<Pt,6>> unit_corners(gs.board.hexes.size());
        for (const auto& hhex : gs.board.hexes) {
            Pt c = Board::axial_to_unit(hhex.q, hhex.r);
            for (int i = 0; i < 6; ++i) {
                double ang = (30.0 + 60.0 * i) * PI / 180.0;
                Pt p{c.x + std::cos(ang), c.y + std::sin(ang)};
                unit_corners[hhex.id][i] = p;
                minx = std::min(minx, p.x); miny = std::min(miny, p.y);
                maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y);
            }
        }
        double bw = layout.board.w * 0.92;
        double bh = layout.board.h * 0.92;
        double scale = std::min(bw / (maxx - minx), bh / (maxy - miny));
        double ox = layout.board.x + layout.board.w / 2.0 - (minx + maxx) * scale / 2.0;
        double oy = layout.board.y + layout.board.h / 2.0 - (miny + maxy) * scale / 2.0;
        for (const auto& hhex : gs.board.hexes) {
            Pt c = Board::axial_to_unit(hhex.q, hhex.r);
            layout.hex_centers[hhex.id] = Pt{ox + c.x * scale, oy + c.y * scale};
            for (int i = 0; i < 6; ++i) layout.hex_corners[hhex.id][i] = Pt{ox + unit_corners[hhex.id][i].x * scale, oy + unit_corners[hhex.id][i].y * scale};
        }
        for (const auto& v : gs.board.vertices) layout.vertex_pts[v.id] = Pt{ox + v.p.x * scale, oy + v.p.y * scale};
    }

    void color(GdkGC* gc, int gray) {
        GdkColor c;
        gray = std::max(0, std::min(255, gray));
        c.red = c.green = c.blue = (gushort)(gray * 257);
        gdk_gc_set_rgb_fg_color(gc, &c);
    }

    void line_width(GdkGC* gc, int width) {
        gdk_gc_set_line_attributes(gc, width, GDK_LINE_SOLID, GDK_CAP_ROUND, GDK_JOIN_ROUND);
    }

    void draw_text(GtkWidget* widget, GdkGC* gc, int x, int y, int w, const std::string& text, int size, bool bold = false, bool center = false) {
        PangoLayout* layout_text = gtk_widget_create_pango_layout(widget, text.c_str());
        PangoFontDescription* font = pango_font_description_new();
        pango_font_description_set_family(font, "Sans");
        pango_font_description_set_size(font, size * PANGO_SCALE);
        pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
        pango_layout_set_font_description(layout_text, font);
        pango_layout_set_width(layout_text, w * PANGO_SCALE);
        pango_layout_set_wrap(layout_text, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_alignment(layout_text, center ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
        gdk_draw_layout(widget->window, gc, x, y, layout_text);
        pango_font_description_free(font);
        g_object_unref(layout_text);
    }

    void draw_button(GtkWidget* widget, GdkGC* gc, const Button& b) {
        color(gc, b.enabled ? 255 : 230);
        gdk_draw_rectangle(widget->window, gc, TRUE, b.r.x, b.r.y, b.r.w, b.r.h);
        color(gc, 0); line_width(gc, 3);
        gdk_draw_rectangle(widget->window, gc, FALSE, b.r.x, b.r.y, b.r.w, b.r.h);
        if (!b.enabled) color(gc, 120);
        int fs = std::max(20, text_size - 6);
        draw_text(widget, gc, b.r.x + 6, b.r.y + (b.r.h - fs) / 2 - 4, b.r.w - 12, b.label, fs, true, true);
        color(gc, 0);
    }

    void draw_header(GtkWidget* widget, GdkGC* gc, const std::string& title) {
        color(gc, 255); gdk_draw_rectangle(widget->window, gc, TRUE, 0, 0, layout.w, layout.top.h);
        color(gc, 0); line_width(gc, 3); gdk_draw_line(widget->window, gc, 0, layout.top.h - 1, layout.w, layout.top.h - 1);
        draw_text(widget, gc, 12, 14, layout.w - 24, title, std::max(28, text_size), true, false);
    }

    void draw_panel_bg(GtkWidget* widget, GdkGC* gc) {
        color(gc, 255); gdk_draw_rectangle(widget->window, gc, TRUE, layout.bottom.x, layout.bottom.y, layout.bottom.w, layout.bottom.h);
        color(gc, 0); line_width(gc, 3); gdk_draw_line(widget->window, gc, 0, layout.bottom.y, layout.w, layout.bottom.y);
    }

    void draw_center_text(GtkWidget* widget, GdkGC* gc, const std::string& title, const std::string& body) {
        color(gc, 255); gdk_draw_rectangle(widget->window, gc, TRUE, 0, 0, layout.w, layout.h);
        color(gc, 0);
        draw_text(widget, gc, 40, layout.h / 5, layout.w - 80, title, std::max(40, text_size + 8), true, true);
        draw_text(widget, gc, 50, layout.h / 5 + 90, layout.w - 100, body, std::max(28, text_size - 2), false, true);
    }

    void draw_resource_symbol(GtkWidget* widget, GdkGC* gc, Resource r, int cx, int cy, int s) {
        color(gc, 0); line_width(gc, 2);
        if (r == Resource::Wood) {
            gdk_draw_line(widget->window, gc, cx, cy - s/2, cx, cy + s/2);
            gdk_draw_line(widget->window, gc, cx, cy - s/4, cx - s/3, cy);
            gdk_draw_line(widget->window, gc, cx, cy - s/4, cx + s/3, cy);
            gdk_draw_arc(widget->window, gc, FALSE, cx - s/2, cy - s/2, s, s/2, 0, 360 * 64);
        } else if (r == Resource::Brick) {
            for (int i = -1; i <= 1; ++i) gdk_draw_line(widget->window, gc, cx - s/2, cy + i*s/5, cx + s/2, cy + i*s/5);
            gdk_draw_rectangle(widget->window, gc, FALSE, cx - s/2, cy - s/3, s, 2*s/3);
            gdk_draw_line(widget->window, gc, cx, cy - s/3, cx, cy - s/15);
            gdk_draw_line(widget->window, gc, cx - s/4, cy - s/15, cx - s/4, cy + s/5);
            gdk_draw_line(widget->window, gc, cx + s/4, cy - s/15, cx + s/4, cy + s/5);
        } else if (r == Resource::Sheep) {
            for (int i = 0; i < 5; ++i) gdk_draw_arc(widget->window, gc, FALSE, cx - s/2 + i*s/5, cy - s/5, s/3, s/3, 0, 360 * 64);
            gdk_draw_arc(widget->window, gc, FALSE, cx - s/3, cy - s/6, 2*s/3, s/2, 0, 360 * 64);
        } else if (r == Resource::Wheat) {
            gdk_draw_line(widget->window, gc, cx, cy + s/2, cx, cy - s/2);
            for (int i = 0; i < 4; ++i) {
                int yy = cy - s/2 + i*s/4;
                gdk_draw_line(widget->window, gc, cx, yy, cx - s/3, yy + s/5);
                gdk_draw_line(widget->window, gc, cx, yy, cx + s/3, yy + s/5);
            }
        } else if (r == Resource::Ore) {
            GdkPoint pts[5] = {{cx - s/2, cy + s/3}, {cx - s/4, cy - s/3}, {cx, cy + s/5}, {cx + s/4, cy - s/2}, {cx + s/2, cy + s/3}};
            gdk_draw_polygon(widget->window, gc, FALSE, pts, 5);
        } else if (r == Resource::Desert) {
            for (int i = -2; i <= 2; ++i) for (int j = -1; j <= 1; ++j) gdk_draw_arc(widget->window, gc, TRUE, cx + i*s/5, cy + j*s/5, 3, 3, 0, 360*64);
        }
    }

    void draw_player_piece(GtkWidget* widget, GdkGC* gc, int owner, int cx, int cy, int s, bool city) {
        if (owner < 0) return;
        color(gc, owner == 1 ? 255 : 0);
        GdkPoint house[5] = {{cx - s, cy}, {cx, cy - s}, {cx + s, cy}, {cx + s, cy + s}, {cx - s, cy + s}};
        gdk_draw_polygon(widget->window, gc, TRUE, house, 5);
        color(gc, 0); line_width(gc, 3);
        gdk_draw_polygon(widget->window, gc, FALSE, house, 5);
        if (city) gdk_draw_rectangle(widget->window, gc, FALSE, cx + s/3, cy - s/2, s, s + s/2);
        if (owner == 2 || owner == 3) {
            for (int dx = -s; dx <= s; dx += 5) gdk_draw_line(widget->window, gc, cx + dx, cy + s, cx + dx + s, cy - s);
        }
        if (owner == 3) {
            for (int dx = -s; dx <= s; dx += 5) gdk_draw_line(widget->window, gc, cx + dx, cy - s, cx + dx + s, cy + s);
        }
    }

    void draw_road(GtkWidget* widget, GdkGC* gc, const Edge& e) {
        if (e.owner < 0) return;
        Pt a = layout.vertex_pts[e.a], b = layout.vertex_pts[e.b];
        int owner = e.owner;
        if (owner == 1) { color(gc, 255); line_width(gc, 12); gdk_draw_line(widget->window, gc, (int)a.x, (int)a.y, (int)b.x, (int)b.y); color(gc, 0); line_width(gc, 3); gdk_draw_line(widget->window, gc, (int)a.x, (int)a.y, (int)b.x, (int)b.y); }
        else { color(gc, 0); line_width(gc, 8); gdk_draw_line(widget->window, gc, (int)a.x, (int)a.y, (int)b.x, (int)b.y); }
        if (owner == 2 || owner == 3) {
            color(gc, 255); line_width(gc, 2);
            double dx = b.x - a.x, dy = b.y - a.y;
            double len = std::max(1.0, std::sqrt(dx*dx + dy*dy));
            for (double t = 0.2; t < 0.9; t += 0.18) {
                int x = (int)(a.x + dx*t), y = (int)(a.y + dy*t);
                gdk_draw_line(widget->window, gc, x - (int)(dy/len*6), y + (int)(dx/len*6), x + (int)(dy/len*6), y - (int)(dx/len*6));
            }
        }
        color(gc,0);
    }

    void draw_board(GtkWidget* widget, GdkGC* gc) {
        color(gc, 255); gdk_draw_rectangle(widget->window, gc, TRUE, layout.board.x, layout.board.y, layout.board.w, layout.board.h);
        for (const auto& h : gs.board.hexes) {
            GdkPoint pts[6];
            for (int i = 0; i < 6; ++i) pts[i] = {(gint)layout.hex_corners[h.id][i].x, (gint)layout.hex_corners[h.id][i].y};
            color(gc, 255); gdk_draw_polygon(widget->window, gc, TRUE, pts, 6);
            color(gc, 0); line_width(gc, 3); gdk_draw_polygon(widget->window, gc, FALSE, pts, 6);
            Pt c = layout.hex_centers[h.id];
            int sym = std::max(24, (int)(std::abs(layout.hex_corners[h.id][0].x - c.x) * 0.70));
            draw_resource_symbol(widget, gc, h.resource, (int)c.x, (int)c.y - 12, sym);
            if (h.number > 0) {
                int ring = (h.number == 6 || h.number == 8) ? 5 : ((h.number == 5 || h.number == 9) ? 3 : 1);
                line_width(gc, ring); color(gc, 0);
                int rr = 28;
                gdk_draw_arc(widget->window, gc, FALSE, (int)c.x - rr, (int)c.y + 18 - rr, rr*2, rr*2, 0, 360*64);
                draw_text(widget, gc, (int)c.x - 24, (int)c.y + 18 - 15, 48, std::to_string(h.number), 24, true, true);
            }
            if (h.bandit) {
                color(gc, 0); line_width(gc, 4);
                gdk_draw_arc(widget->window, gc, TRUE, (int)c.x - 13, (int)c.y - 54, 26, 26, 0, 360*64);
                gdk_draw_rectangle(widget->window, gc, TRUE, (int)c.x - 16, (int)c.y - 30, 32, 38);
                color(gc, 255); draw_text(widget, gc, (int)c.x - 16, (int)c.y - 28, 32, "B", 20, true, true); color(gc, 0);
            }
        }
        for (const auto& e : gs.board.edges) draw_road(widget, gc, e);
        for (const auto& v : gs.board.vertices) {
            if (v.owner >= 0) draw_player_piece(widget, gc, v.owner, (int)layout.vertex_pts[v.id].x, (int)layout.vertex_pts[v.id].y, v.building == BuildType::City ? 13 : 10, v.building == BuildType::City);
        }
        draw_selection_hints(widget, gc);
    }

    void draw_selection_hints(GtkWidget* widget, GdkGC* gc) {
        color(gc, 0);
        if (screen == Screen::SelectSettlement || (gs.initial_phase && gs.placing_settlement && screen == Screen::Game)) {
            for (const auto& v : gs.board.vertices) if (gs.valid_settlement(v.id, gs.current_player)) {
                Pt p = layout.vertex_pts[v.id]; line_width(gc, 2); gdk_draw_arc(widget->window, gc, FALSE, (int)p.x - 13, (int)p.y - 13, 26, 26, 0, 360*64);
            }
        }
        if (screen == Screen::SelectRoad || (gs.initial_phase && !gs.placing_settlement && screen == Screen::Game)) {
            line_width(gc, 3);
            for (const auto& e : gs.board.edges) if (gs.valid_road(e.id, gs.current_player)) {
                Pt a = layout.vertex_pts[e.a], b = layout.vertex_pts[e.b];
                gdk_draw_line(widget->window, gc, (int)a.x, (int)a.y, (int)b.x, (int)b.y);
            }
        }
        if (screen == Screen::SelectCity) {
            for (const auto& v : gs.board.vertices) if (gs.valid_city(v.id, gs.current_player)) {
                Pt p = layout.vertex_pts[v.id]; line_width(gc, 4); gdk_draw_rectangle(widget->window, gc, FALSE, (int)p.x - 18, (int)p.y - 18, 36, 36);
            }
        }
        if (screen == Screen::MoveBandit) {
            for (const auto& h : gs.board.hexes) if (!h.bandit) {
                Pt c = layout.hex_centers[h.id]; line_width(gc, 3); gdk_draw_arc(widget->window, gc, FALSE, (int)c.x - 38, (int)c.y - 38, 76, 76, 0, 360*64);
            }
        }
    }

    std::string status_line() const {
        if (!gs.active) return "Kindle Settlers";
        std::ostringstream s;
        s << "P" << gs.current_player + 1 << " Turn | Roll: " << (gs.last_roll ? std::to_string(gs.last_roll) : "-")
          << " | Cards: " << gs.resource_count(gs.current_player)
          << " | Points: " << gs.visible_points(gs.current_player);
        return s.str();
    }

    void draw_game(GtkWidget* widget, GdkGC* gc) {
        draw_header(widget, gc, status_line());
        add_button(layout.w - 168, 10, 70, 52, "Menu", Action::Menu);
        add_button(layout.w - 88, 10, 76, 52, "Log", Action::Log);
        draw_board(widget, gc);
        draw_panel_bg(widget, gc);
        int y = layout.bottom.y + 14;
        int x = 16;
        int colw = layout.w / 5;
        if (gs.initial_phase) {
            std::string step = std::string("Initial Placement: P") + std::to_string(gs.current_player + 1) + (gs.placing_settlement ? " tap a valid corner for a settlement." : " tap an adjacent edge for a road.");
            draw_text(widget, gc, 16, y, layout.w - 32, step, std::max(24, text_size - 4), true);
            draw_text(widget, gc, 16, y + 48, layout.w - 32, message, std::max(20, text_size - 8), false);
        } else {
            draw_resources(widget, gc, x, y);
            int by = layout.bottom.y + layout.bottom.h - 130;
            int bw = (layout.w - 40) / 3;
            if (!gs.dice_rolled) add_button(12, by, bw, 52, "Roll Dice", Action::RollDice);
            else add_button(12, by, bw, 52, "Build", Action::Build);
            add_button(24 + bw, by, bw, 52, "Cards M2", Action::None, false);
            add_button(36 + 2*bw, by, bw, 52, "End Turn", Action::EndTurn, gs.dice_rolled);
            add_button(12, by + 64, bw, 52, "Trade M2", Action::None, false);
            add_button(24 + bw, by + 64, bw, 52, "Rules", Action::Rules);
            add_button(36 + 2*bw, by + 64, bw, 52, "Save", Action::SaveGame);
            if (!message.empty()) draw_text(widget, gc, 16, layout.bottom.y + 90, layout.w - 32, message, std::max(20, text_size - 10), false);
        }
    }

    void draw_resources(GtkWidget* widget, GdkGC* gc, int x, int y) {
        const auto& p = gs.players[gs.current_player];
        std::ostringstream s;
        s << "Your Resources  Wood " << p.res[0] << " | Brick " << p.res[1] << " | Sheep " << p.res[2] << " | Wheat " << p.res[3] << " | Ore " << p.res[4];
        draw_text(widget, gc, x, y, layout.w - 32, s.str(), std::max(22, text_size - 8), true);
        std::ostringstream other;
        for (int i = 0; i < gs.player_count; ++i) if (i != gs.current_player) {
            other << "P" << i + 1 << ": " << gs.resource_count(i) << " cards, " << gs.players[i].progress_count << " progress, " << gs.visible_points(i) << " pts   ";
        }
        draw_text(widget, gc, x, y + 42, layout.w - 32, other.str(), std::max(20, text_size - 10), false);
        std::ostringstream bonus;
        bonus << "Longest Route: " << (gs.longest_route_holder >= 0 ? "P" + std::to_string(gs.longest_route_holder + 1) + " length " + std::to_string(gs.longest_route_length) : "none")
              << " | Largest Patrol: " << (gs.largest_patrol_holder >= 0 ? "P" + std::to_string(gs.largest_patrol_holder + 1) : "none");
        draw_text(widget, gc, x, y + 74, layout.w - 32, bonus.str(), std::max(18, text_size - 12), false);
    }

    void draw_menu(GtkWidget* widget, GdkGC* gc) {
        draw_center_text(widget, gc, "Kindle Settlers", "Native KUAL hotseat island game");
        int bw = layout.w - 120, bh = 62, x = 60, y = layout.h / 2 - 40;
        bool has_save = file_exists(save_path());
        add_button(x, y, bw, bh, "Resume Game", Action::Resume, has_save);
        add_button(x, y + 76, bw, bh, "New Game", Action::NewGame);
        add_button(x, y + 152, bw, bh, "Rules Reference", Action::Rules);
        add_button(x, y + 228, bw, bh, "Settings", Action::Settings);
        add_button(x, y + 304, bw, bh, "Exit to KUAL", Action::Exit);
    }

    void draw_setup(GtkWidget* widget, GdkGC* gc) {
        draw_center_text(widget, gc, "New Game", "Choose local hotseat player count.");
        int y = layout.h / 2 - 40;
        int bw = (layout.w - 100) / 3;
        add_button(30, y, bw, 70, "2", Action::SetupPlayers2, setup_players == 2);
        add_button(50 + bw, y, bw, 70, "3", Action::SetupPlayers3, setup_players == 3);
        add_button(70 + 2*bw, y, bw, 70, "4", Action::SetupPlayers4, setup_players == 4);
        draw_text(widget, gc, 50, y + 92, layout.w - 100, "Board: Random Standard | Victory: 10 Points", std::max(24, text_size - 4), false, true);
        add_button(60, y + 155, layout.w - 120, 68, "Start Game", Action::StartGame);
        add_button(60, y + 235, layout.w - 120, 68, "Back", Action::Back);
    }

    void draw_handoff(GtkWidget* widget, GdkGC* gc) {
        std::ostringstream body;
        body << "Pass Kindle to Player " << handoff_player + 1 << "\n\nPlayer " << handoff_player + 1 << ", tap Confirm when ready.";
        draw_center_text(widget, gc, "Private Handoff", body.str());
        add_button(80, layout.h - 180, layout.w - 160, 76, "Confirm", Action::ConfirmHandoff);
    }

    void draw_build_menu(GtkWidget* widget, GdkGC* gc) {
        draw_game(widget, gc);
        color(gc, 255); gdk_draw_rectangle(widget->window, gc, TRUE, 20, layout.bottom.y + 20, layout.w - 40, layout.bottom.h - 40);
        color(gc, 0); line_width(gc, 3); gdk_draw_rectangle(widget->window, gc, FALSE, 20, layout.bottom.y + 20, layout.w - 40, layout.bottom.h - 40);
        draw_text(widget, gc, 38, layout.bottom.y + 34, layout.w - 76, "Build", text_size, true);
        int y = layout.bottom.y + 86;
        add_button(38, y, layout.w - 76, 48, "Road  | Wood 1 + Brick 1", Action::BuildRoad, gs.can_afford(gs.current_player, BuildType::Road));
        add_button(38, y + 58, layout.w - 76, 48, "Settlement | Wood + Brick + Sheep + Wheat", Action::BuildSettlement, gs.can_afford(gs.current_player, BuildType::Settlement));
        add_button(38, y + 116, layout.w - 76, 48, "City | Wheat 2 + Ore 3", Action::BuildCity, gs.can_afford(gs.current_player, BuildType::City));
        add_button(38, y + 174, layout.w - 76, 48, "Back", Action::Back);
    }

    void draw_roll(GtkWidget* widget, GdkGC* gc) {
        draw_game(widget, gc);
        color(gc, 255); gdk_draw_rectangle(widget->window, gc, TRUE, 48, layout.h/3, layout.w - 96, 250);
        color(gc, 0); line_width(gc, 4); gdk_draw_rectangle(widget->window, gc, FALSE, 48, layout.h/3, layout.w - 96, 250);
        draw_text(widget, gc, 70, layout.h/3 + 28, layout.w - 140, "Dice Roll", text_size + 4, true, true);
        draw_text(widget, gc, 70, layout.h/3 + 92, layout.w - 140, "Result: " + std::to_string(gs.last_roll), text_size + 12, true, true);
        std::string note = gs.last_roll == 7 ? "Bandit activated." : "Resources produced.";
        draw_text(widget, gc, 70, layout.h/3 + 152, layout.w - 140, note, text_size - 4, false, true);
        add_button(90, layout.h/3 + 190, layout.w - 180, 58, "Continue", Action::Back);
    }

    void draw_confirm_build(GtkWidget* widget, GdkGC* gc) {
        draw_game(widget, gc);
        std::string label = "Build here?";
        if (pending_type == BuildType::Road) label = "Build road here?";
        if (pending_type == BuildType::Settlement) label = "Build settlement here?";
        if (pending_type == BuildType::City) label = "Upgrade this settlement?";
        color(gc, 255); gdk_draw_rectangle(widget->window, gc, TRUE, 54, layout.h/3, layout.w - 108, 220);
        color(gc, 0); line_width(gc, 4); gdk_draw_rectangle(widget->window, gc, FALSE, 54, layout.h/3, layout.w - 108, 220);
        draw_text(widget, gc, 80, layout.h/3 + 40, layout.w - 160, label, text_size + 2, true, true);
        add_button(90, layout.h/3 + 130, (layout.w - 220) / 2, 58, "Confirm", Action::ConfirmBuild);
        add_button(120 + (layout.w - 220) / 2, layout.h/3 + 130, (layout.w - 220) / 2, 58, "Cancel", Action::CancelBuild);
    }

    void draw_log(GtkWidget* widget, GdkGC* gc) {
        color(gc,255); gdk_draw_rectangle(widget->window, gc, TRUE, 0,0,layout.w,layout.h);
        color(gc,0); draw_text(widget, gc, 20, 18, layout.w - 40, "Action Log", text_size + 2, true);
        int y = 80;
        int start = std::max(0, (int)gs.log.size() - 14);
        for (int i = start; i < (int)gs.log.size(); ++i) {
            draw_text(widget, gc, 24, y, layout.w - 48, gs.log[i], std::max(20, text_size - 8), false);
            y += std::max(34, text_size + 2);
        }
        add_button(60, layout.h - 110, layout.w - 120, 64, "Back", Action::Back);
    }

    void draw_rules(GtkWidget* widget, GdkGC* gc) {
        color(gc,255); gdk_draw_rectangle(widget->window, gc, TRUE, 0,0,layout.w,layout.h);
        color(gc,0);
        draw_text(widget, gc, 20, 16, layout.w - 40, "Rules Reference", text_size + 2, true);
        std::string rules =
            "Goal: reach 10 points on your turn.\n\n"
            "Turn: roll, produce resources, trade/build, then end turn.\n\n"
            "Build Costs: Road = Wood + Brick. Settlement = Wood + Brick + Sheep + Wheat. City = 2 Wheat + 3 Ore.\n\n"
            "Bandit: a 7 blocks production on one hex. Full discard/steal flow is second milestone.\n\n"
            "Longest Route: first player with a route of at least 5 roads and longer than the current holder gains 2 points.\n\n"
            "Progress cards, bank trades, player trades, discard flow, and Largest Patrol are reserved for milestone 2.";
        draw_text(widget, gc, 28, 76, layout.w - 56, rules, std::max(20, text_size - 8), false);
        add_button(60, layout.h - 110, layout.w - 120, 64, "Back", Action::Back);
    }

    void draw_settings(GtkWidget* widget, GdkGC* gc) {
        draw_center_text(widget, gc, "Settings", "Handoff privacy is On for hotseat mode.");
        int y = layout.h / 2 - 70;
        add_button(60, y, layout.w - 120, 64, "Text Size -", Action::TextMinus);
        draw_text(widget, gc, 60, y + 78, layout.w - 120, "Current text size: " + std::to_string(text_size), text_size - 4, true, true);
        add_button(60, y + 125, layout.w - 120, 64, "Text Size +", Action::TextPlus);
        add_button(60, y + 205, layout.w - 120, 64, "Back", Action::Back);
    }

    void draw_game_over(GtkWidget* widget, GdkGC* gc) {
        int p = gs.current_player;
        std::ostringstream body;
        body << "Visible Points: " << gs.visible_points(p) << "\nHidden Points: " << gs.players[p].hidden_points << "\nTotal: " << gs.total_points(p);
        draw_center_text(widget, gc, "Player " + std::to_string(p + 1) + " Wins", body.str());
        int y = layout.h - 270;
        add_button(70, y, layout.w - 140, 64, "New Game", Action::NewGame);
        add_button(70, y + 78, layout.w - 140, 64, "View Board", Action::ViewBoard);
        add_button(70, y + 156, layout.w - 140, 64, "Exit to KUAL", Action::Exit);
    }

    gboolean expose(GtkWidget* widget) {
        GtkAllocation a = widget->allocation;
        compute_layout(a.width, a.height);
        buttons.clear();
        GdkGC* gc = gdk_gc_new(widget->window);
        color(gc, 255); gdk_draw_rectangle(widget->window, gc, TRUE, 0, 0, a.width, a.height);
        color(gc, 0);
        if (screen == Screen::Menu) draw_menu(widget, gc);
        else if (screen == Screen::Setup) draw_setup(widget, gc);
        else if (screen == Screen::Handoff) draw_handoff(widget, gc);
        else if (screen == Screen::Game || screen == Screen::SelectRoad || screen == Screen::SelectSettlement || screen == Screen::SelectCity || screen == Screen::MoveBandit) draw_game(widget, gc);
        else if (screen == Screen::BuildMenu) draw_build_menu(widget, gc);
        else if (screen == Screen::ConfirmBuild) draw_confirm_build(widget, gc);
        else if (screen == Screen::RollResult) draw_roll(widget, gc);
        else if (screen == Screen::Log) draw_log(widget, gc);
        else if (screen == Screen::Rules) draw_rules(widget, gc);
        else if (screen == Screen::Settings) draw_settings(widget, gc);
        else if (screen == Screen::GameOver) draw_game_over(widget, gc);
        for (const auto& b : buttons) draw_button(widget, gc, b);
        g_object_unref(gc);
        return TRUE;
    }

    int nearest_vertex(int x, int y, double maxd = 28.0) const {
        int best = -1; double bd = maxd;
        for (size_t i = 0; i < layout.vertex_pts.size(); ++i) {
            double dx = layout.vertex_pts[i].x - x, dy = layout.vertex_pts[i].y - y;
            double d = std::sqrt(dx*dx + dy*dy);
            if (d < bd) { bd = d; best = (int)i; }
        }
        return best;
    }

    static double dist_to_seg(Pt p, Pt a, Pt b) {
        double vx = b.x - a.x, vy = b.y - a.y;
        double wx = p.x - a.x, wy = p.y - a.y;
        double c1 = vx*wx + vy*wy;
        if (c1 <= 0) return std::sqrt((p.x-a.x)*(p.x-a.x)+(p.y-a.y)*(p.y-a.y));
        double c2 = vx*vx + vy*vy;
        if (c2 <= c1) return std::sqrt((p.x-b.x)*(p.x-b.x)+(p.y-b.y)*(p.y-b.y));
        double t = c1 / c2;
        double px = a.x + t*vx, py = a.y + t*vy;
        return std::sqrt((p.x-px)*(p.x-px)+(p.y-py)*(p.y-py));
    }

    int nearest_edge(int x, int y, double maxd = 22.0) const {
        int best = -1; double bd = maxd;
        Pt p{(double)x,(double)y};
        for (const auto& e : gs.board.edges) {
            double d = dist_to_seg(p, layout.vertex_pts[e.a], layout.vertex_pts[e.b]);
            if (d < bd) { bd = d; best = e.id; }
        }
        return best;
    }

    int nearest_hex(int x, int y, double maxd = 60.0) const {
        int best = -1; double bd = maxd;
        for (size_t i = 0; i < layout.hex_centers.size(); ++i) {
            double dx = layout.hex_centers[i].x - x, dy = layout.hex_centers[i].y - y;
            double d = std::sqrt(dx*dx + dy*dy);
            if (d < bd) { bd = d; best = (int)i; }
        }
        return best;
    }

    void handle_click(int x, int y) {
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            const auto& b = *it;
            if (b.r.contains(x, y)) {
                if (b.enabled) handle_action(b.action);
                return;
            }
        }
        if (screen == Screen::Game && gs.initial_phase) {
            if (gs.placing_settlement) {
                int v = nearest_vertex(x,y);
                if (v >= 0 && gs.valid_settlement(v, gs.current_player)) { pending_vertex = v; pending_type = BuildType::Settlement; screen = Screen::ConfirmBuild; message.clear(); }
                else message = "Invalid settlement placement.";
                redraw(); return;
            } else {
                int e = nearest_edge(x,y);
                if (e >= 0 && gs.valid_road(e, gs.current_player)) { pending_edge = e; pending_type = BuildType::Road; screen = Screen::ConfirmBuild; message.clear(); }
                else message = "Invalid road placement.";
                redraw(); return;
            }
        }
        if (screen == Screen::SelectRoad) {
            int e = nearest_edge(x,y);
            if (e >= 0 && gs.valid_road(e, gs.current_player)) { pending_edge = e; pending_type = BuildType::Road; screen = Screen::ConfirmBuild; message.clear(); }
            else message = "Invalid road placement.";
            redraw(); return;
        }
        if (screen == Screen::SelectSettlement) {
            int v = nearest_vertex(x,y);
            if (v >= 0 && gs.valid_settlement(v, gs.current_player)) { pending_vertex = v; pending_type = BuildType::Settlement; screen = Screen::ConfirmBuild; message.clear(); }
            else message = "Invalid settlement placement.";
            redraw(); return;
        }
        if (screen == Screen::SelectCity) {
            int v = nearest_vertex(x,y);
            if (v >= 0 && gs.valid_city(v, gs.current_player)) { pending_vertex = v; pending_type = BuildType::City; screen = Screen::ConfirmBuild; message.clear(); }
            else message = "Select one of your settlements.";
            redraw(); return;
        }
        if (screen == Screen::MoveBandit) {
            int h = nearest_hex(x,y);
            if (h >= 0 && !gs.board.hexes[h].bandit) {
                gs.move_bandit(h); gs.save(); screen = Screen::RollResult;
            } else message = "Select a different hex.";
            redraw(); return;
        }
    }

    void handle_action(Action a) {
        switch (a) {
            case Action::Resume:
                if (gs.load()) { handoff_player = gs.current_player; screen = Screen::Handoff; }
                break;
            case Action::NewGame:
                screen = Screen::Setup; break;
            case Action::Rules:
                previous_screen = screen; screen = Screen::Rules; break;
            case Action::Settings:
                previous_screen = screen; screen = Screen::Settings; break;
            case Action::Exit:
                if (gs.active) gs.save(); gtk_main_quit(); return;
            case Action::Back:
                if (screen == Screen::Rules || screen == Screen::Settings || screen == Screen::Log) screen = previous_screen;
                else if (screen == Screen::Setup) screen = Screen::Menu;
                else if (screen == Screen::BuildMenu) screen = Screen::Game;
                else if (screen == Screen::RollResult) screen = Screen::Game;
                else screen = Screen::Game;
                break;
            case Action::SetupPlayers2: setup_players = 2; break;
            case Action::SetupPlayers3: setup_players = 3; break;
            case Action::SetupPlayers4: setup_players = 4; break;
            case Action::StartGame:
                gs.reset(setup_players); gs.save(); handoff_player = gs.current_player; screen = Screen::Handoff; break;
            case Action::ConfirmHandoff:
                gs.current_player = handoff_player; screen = Screen::Game; message.clear(); break;
            case Action::RollDice:
                if (!gs.dice_rolled) { gs.roll_dice(rng); gs.save(); screen = (gs.last_roll == 7 ? Screen::MoveBandit : Screen::RollResult); }
                break;
            case Action::Build:
                if (!gs.dice_rolled) message = "You must roll before building.";
                else screen = Screen::BuildMenu;
                break;
            case Action::EndTurn:
                if (!gs.dice_rolled) { message = "Roll before ending turn."; break; }
                gs.end_turn(); gs.save(); handoff_player = gs.current_player; screen = handoff_privacy ? Screen::Handoff : Screen::Game; break;
            case Action::Log:
                previous_screen = screen; screen = Screen::Log; break;
            case Action::Menu:
                if (gs.active) gs.save(); screen = Screen::Menu; break;
            case Action::SaveGame:
                gs.save(); message = "Game saved."; break;
            case Action::BuildRoad:
                if (gs.can_afford(gs.current_player, BuildType::Road)) screen = Screen::SelectRoad; else message = "Not enough resources."; break;
            case Action::BuildSettlement:
                if (gs.can_afford(gs.current_player, BuildType::Settlement)) screen = Screen::SelectSettlement; else message = "Not enough resources."; break;
            case Action::BuildCity:
                if (gs.can_afford(gs.current_player, BuildType::City)) screen = Screen::SelectCity; else message = "Not enough resources."; break;
            case Action::ConfirmBuild:
                commit_pending_build(); break;
            case Action::CancelBuild:
                pending_edge = pending_vertex = -1; pending_type = BuildType::None; screen = gs.initial_phase ? Screen::Game : Screen::BuildMenu; break;
            case Action::ViewBoard:
                screen = Screen::Game; break;
            case Action::TextMinus:
                text_size = std::max(24, text_size - 2); save_settings(); break;
            case Action::TextPlus:
                text_size = std::min(54, text_size + 2); save_settings(); break;
            default: break;
        }
        redraw();
    }

    void commit_pending_build() {
        int p = gs.current_player;
        bool ok = false;
        if (pending_type == BuildType::Road && pending_edge >= 0 && gs.valid_road(pending_edge, p)) { gs.build_road(pending_edge, p); ok = true; }
        if (pending_type == BuildType::Settlement && pending_vertex >= 0 && gs.valid_settlement(pending_vertex, p)) { gs.build_settlement(pending_vertex, p); ok = true; }
        if (pending_type == BuildType::City && pending_vertex >= 0 && gs.valid_city(pending_vertex, p)) { gs.build_city(pending_vertex, p); ok = true; }
        pending_edge = pending_vertex = -1; pending_type = BuildType::None;
        if (!ok) { message = "Invalid build selection."; screen = Screen::Game; return; }
        if (gs.initial_phase) {
            gs.advance_initial();
            gs.save();
            if (gs.initial_phase) { handoff_player = gs.current_player; screen = Screen::Handoff; }
            else { handoff_player = gs.current_player; screen = Screen::Handoff; }
        } else {
            gs.save();
            if (gs.total_points(p) >= 10) screen = Screen::GameOver;
            else screen = Screen::Game;
        }
    }
};

static App* g_app = nullptr;

static gboolean on_expose(GtkWidget* widget, GdkEventExpose*, gpointer) { return g_app->expose(widget); }
static gboolean on_button(GtkWidget*, GdkEventButton* event, gpointer) {
    if (event->type == GDK_BUTTON_PRESS) g_app->handle_click((int)event->x, (int)event->y);
    return TRUE;
}
static gboolean on_delete(GtkWidget*, GdkEvent*, gpointer) {
    if (g_app && g_app->gs.active) g_app->gs.save();
    gtk_main_quit();
    return TRUE;
}

static gboolean force_window_visible(gpointer data) {
    App* app = static_cast<App*>(data);
    if (!app || !app->window) return FALSE;
    gtk_window_set_decorated(GTK_WINDOW(app->window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(app->window), TRUE);
    gtk_window_fullscreen(GTK_WINDOW(app->window));
    gtk_window_present(GTK_WINDOW(app->window));
    if (app->window->window) {
        gdk_window_fullscreen(app->window->window);
        gdk_window_raise(app->window->window);
    }
    if (app->area) gtk_widget_queue_draw(app->area);
    append_app_log("window-present requested");
    return FALSE;
}

static void on_realize(GtkWidget*, gpointer data) {
    force_window_visible(data);
}

int main(int argc, char** argv) {
    append_app_log("process start");
    if (!gtk_init_check(&argc, &argv)) {
        append_app_log("gtk_init_check failed: unable to open Kindle X display");
        return 2;
    }
    App app;
    g_app = &app;
    app.load_settings();
    app.gs.board.build_topology();

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Kindle Settlers");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 758, 1024);
    gtk_window_set_decorated(GTK_WINDOW(app.window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(app.window), TRUE);
    gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);
    gtk_window_fullscreen(GTK_WINDOW(app.window));

    app.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.area, 758, 1024);
    gtk_container_add(GTK_CONTAINER(app.window), app.area);
    gtk_widget_add_events(app.area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(app.area), "expose-event", G_CALLBACK(on_expose), nullptr);
    g_signal_connect(G_OBJECT(app.area), "button-press-event", G_CALLBACK(on_button), nullptr);
    g_signal_connect(G_OBJECT(app.window), "delete-event", G_CALLBACK(on_delete), nullptr);
    g_signal_connect(G_OBJECT(app.window), "realize", G_CALLBACK(on_realize), &app);

    gtk_widget_show_all(app.window);
    gtk_widget_grab_focus(app.area);
    force_window_visible(&app);
    g_idle_add(force_window_visible, &app);
    g_timeout_add(250, force_window_visible, &app);
    g_timeout_add(1500, force_window_visible, &app);
    append_app_log("gtk main entering");
    gtk_main();
    append_app_log("gtk main exited");
    return 0;
}
