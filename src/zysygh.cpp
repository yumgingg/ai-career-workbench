// =============================================================================
//  zysygh.cpp —— 人工智能专业 · 职业生涯规划学习实践工作台
//                AI Career Planning & Practice Workbench (single file, C++17)
//
//  设计目标：给人工智能专业学生一个"能落地"的规划工具——不是空泛的职业测评，
//            而是把「我是谁 → 我要去哪 → 差什么 → 学什么 → 做什么 → 进展如何」
//            串成一条可执行、可追踪、可存档的闭环。
//
//  模块：
//    1. 个人档案与技能自评（13 项 AI 核心能力，0~5 分）
//    2. 岗位图谱与加权匹配（7 类 AI 岗位，输出匹配度 + 技能差距）
//    3. 四阶段学习路线（基础夯实 → 核心进阶 → 项目实战 → 求职冲刺）
//       并可一键把路线落成带截止日期的任务
//    4. 任务管理（新增 / 完成 / 撤回 / 删除 / 筛选 / 逾期提醒 / 工时统计）
//    5. 实践项目台账（阶段推进 / 技术栈 / 仓库链接 / 复盘备注）
//    6. 数据看板（完成率、分类条形图、技能画像、项目阶段分布、本周建议）
//    7. 本地存档 / 读档 / 示例数据 / 清空
//
//  编译（MinGW-w64 / MSYS2）：
//      g++ -std=c++17 -O2 -o zysygh zysygh.cpp
//  运行：
//      ./zysygh            （数据文件 workbench_data.txt 保存在当前目录）
// =============================================================================

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// =============================================================================
//  一、基础工具：UTF-8 显示宽度、字符串处理
// =============================================================================

static bool g_inputClosed = false;  // 输入流结束（管道/重定向场景），用于安全退出

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

// 存档字段清洗：不允许出现分隔符与换行
std::string clean(std::string s) {
    s = trim(s);
    for (char& c : s) {
        if (c == '|') c = '/';
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    }
    return s;
}

size_t utf8Len(unsigned char c) {
    if ((c & 0x80u) == 0) return 1;
    if ((c & 0xE0u) == 0xC0u) return 2;
    if ((c & 0xF0u) == 0xE0u) return 3;
    if ((c & 0xF8u) == 0xF0u) return 4;
    return 1;
}

size_t displayWidth(const std::string& s) {
    size_t w = 0, i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = utf8Len(c);
        unsigned cp = c;
        if (n == 2 && i + 1 < s.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        } else if (n == 3 && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        } else if (n == 4) {
            cp = 0x4E00;  // emoji 等按宽字符处理
        }
        const bool wide = (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
                          (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                          (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
                          (cp >= 0xFFE0 && cp <= 0xFFE6);
        w += wide ? 2 : 1;
        i += n;
    }
    return w;
}

std::string padRight(const std::string& s, size_t width) {
    const size_t w = displayWidth(s);
    return s + std::string(w < width ? width - w : 0, ' ');
}

std::string padLeft(const std::string& s, size_t width) {
    const size_t w = displayWidth(s);
    return std::string(w < width ? width - w : 0, ' ') + s;
}

std::string clipTo(const std::string& s, size_t maxWidth) {
    if (displayWidth(s) <= maxWidth) return s;
    std::string out;
    size_t w = 0, i = 0;
    while (i < s.size()) {
        const size_t n = utf8Len(static_cast<unsigned char>(s[i]));
        const std::string cp = s.substr(i, n);
        const size_t cw = displayWidth(cp);
        if (w + cw + 2 > maxWidth) break;
        out += cp;
        w += cw;
        i += n;
    }
    return out + "..";
}

std::string spaces(size_t n) { return std::string(n, ' '); }

// =============================================================================
//  二、日期：只处理"日"粒度，用序列号算法避免时区/DST 问题
// =============================================================================

struct Date {
    int y = 1970;
    int m = 1;
    int d = 1;
};

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int daysInMonth(int y, int m) {
    static const int table[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && isLeapYear(y)) return 29;
    return table[m - 1];
}

// 公历日期 → 距 1970-01-01 的天数（Howard Hinnant 算法）
long long toSerial(const Date& dt) {
    long long y = dt.y;
    const long long m = dt.m;
    const long long d = dt.d;
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

Date fromSerial(long long z) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long d = doy - (153 * mp + 2) / 5 + 1;
    const long long m = mp + (mp < 10 ? 3 : -9);
    return Date{static_cast<int>(y + (m <= 2)), static_cast<int>(m), static_cast<int>(d)};
}

Date todayDate() {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    return Date{tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday};
}

Date addDays(const Date& dt, int days) { return fromSerial(toSerial(dt) + days); }

long long daysUntil(const Date& dt) { return toSerial(dt) - toSerial(todayDate()); }

const char* weekdayName(const Date& dt) {
    static const char* names[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const long long s = toSerial(dt);
    return names[static_cast<int>(((s % 7) + 11) % 7)];
}

std::string dateStr(const Date& dt) {
    std::ostringstream os;
    os << dt.y << '-' << std::setw(2) << std::setfill('0') << dt.m << '-' << std::setw(2) << std::setfill('0')
       << dt.d;
    return os.str();
}

std::string dateStrWithWeek(const Date& dt) { return dateStr(dt) + " " + weekdayName(dt); }

bool parseDate(const std::string& text, Date& out) {
    const std::string s = trim(text);
    if (s.empty()) return false;
    int y = 0, m = 0, d = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) == 3 ||
        std::sscanf(s.c_str(), "%d/%d/%d", &y, &m, &d) == 3) {
        if (y < 100) y += 2000;
    } else if (std::sscanf(s.c_str(), "%d-%d", &m, &d) == 2) {
        y = todayDate().y;
    } else if (std::sscanf(s.c_str(), "%d/%d", &m, &d) == 2) {
        y = todayDate().y;
    } else {
        return false;
    }
    if (m < 1 || m > 12 || d < 1 || d > daysInMonth(y, m)) return false;
    out = Date{y, m, d};
    return true;
}

// 剩余天数 → 可读文案
std::string remainText(long long days) {
    if (days < 0) return "逾期 " + std::to_string(-days) + " 天";
    if (days == 0) return "今天到期";
    return "剩 " + std::to_string(days) + " 天";
}

int percentOf(int part, int total) {
    if (total <= 0) return 0;
    return static_cast<int>(std::lround(100.0 * part / total));
}

// =============================================================================
//  三、数据模型
// =============================================================================

struct Skill {
    std::string name;
    int level = 0;  // 0~5
};

struct RoleNeed {
    std::string skill;
    int weight = 1;  // 1 加分项 / 2 重要 / 3 核心
};

struct Role {
    std::string name;
    std::string summary;
    std::vector<RoleNeed> needs;
};

struct Task {
    int id = 0;
    std::string title;
    std::string category;  // 课程 / 项目 / 竞赛 / 实习 / 科研 / 求职 / 其他
    Date due;
    bool done = false;
    double hours = 0.0;  // 预估投入工时
};

struct Project {
    int id = 0;
    std::string name;
    std::string stack;
    int stage = 0;  // 0 构思 / 1 进行中 / 2 已完成 / 3 已上线·开源
    std::string link;
    std::string note;
};

struct Profile {
    std::string name = "同学";
    int grade = 3;  // 1 大一 ... 4 大四 / 5 研一 ... 7 研三
    int weeklyHours = 10;
    std::string targetRole;
    std::vector<Skill> skills;
};

struct Workbench {
    Profile profile;
    std::vector<Task> tasks;
    std::vector<Project> projects;
    int nextTaskId = 1;
    int nextProjectId = 1;
    std::string dataPath = "workbench_data.txt";
};

struct Gap {
    std::string skill;
    int cur = 0;
    int need = 0;
    int weight = 1;
};

struct RoadmapItem {
    std::string skill;  // 为空表示通用行动
    std::string text;
};

struct RoadmapStage {
    std::string title;
    int startWeek = 1;
    int endWeek = 1;
    std::vector<RoadmapItem> items;
};

// =============================================================================
//  四、知识库：技能目录 + 岗位图谱
// =============================================================================

const std::vector<std::string>& skillCatalog() {
    static const std::vector<std::string> names = {
        "数学与统计基础", "Python 编程",   "数据结构与算法", "机器学习",
        "深度学习",       "计算机视觉",    "自然语言处理",   "大模型与提示工程",
        "数据工程与 SQL", "MLOps 与工程化", "论文阅读与复现", "产品与业务理解",
        "沟通与表达"};
    return names;
}

int skillIndex(const std::string& name) {
    const auto& names = skillCatalog();
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}

const std::vector<Role>& roleCatalog() {
    static const std::vector<Role> roles = {
        {"机器学习算法工程师", "训练与调优模型，把算法落到真实业务指标上",
         {{"数学与统计基础", 3},
          {"Python 编程", 3},
          {"数据结构与算法", 2},
          {"机器学习", 3},
          {"深度学习", 3},
          {"数据工程与 SQL", 1},
          {"论文阅读与复现", 1},
          {"MLOps 与工程化", 1}}},
        {"计算机视觉工程师", "检测、分割、OCR、多模态视觉的工程落地",
         {{"Python 编程", 3},
          {"深度学习", 3},
          {"计算机视觉", 3},
          {"数学与统计基础", 2},
          {"论文阅读与复现", 2},
          {"MLOps 与工程化", 2},
          {"数据结构与算法", 1}}},
        {"NLP / 大模型工程师", "文本理解、RAG、微调与 Agent 应用开发",
         {{"Python 编程", 3},
          {"深度学习", 3},
          {"自然语言处理", 3},
          {"大模型与提示工程", 3},
          {"论文阅读与复现", 2},
          {"MLOps 与工程化", 2},
          {"数据结构与算法", 1}}},
        {"数据科学家", "用统计与数据驱动业务决策，偏分析与实验设计",
         {{"数学与统计基础", 3},
          {"Python 编程", 3},
          {"数据工程与 SQL", 3},
          {"机器学习", 3},
          {"产品与业务理解", 2},
          {"沟通与表达", 2}}},
        {"MLOps / AI 平台工程师", "训练平台、推理服务与模型工程化基础设施",
         {{"MLOps 与工程化", 3},
          {"Python 编程", 2},
          {"数据结构与算法", 2},
          {"数据工程与 SQL", 2},
          {"深度学习", 1},
          {"沟通与表达", 1}}},
        {"AI 产品经理", "定义 AI 产品，评估可行性、体验与商业价值",
         {{"产品与业务理解", 3},
          {"沟通与表达", 3},
          {"大模型与提示工程", 2},
          {"机器学习", 1},
          {"数据工程与 SQL", 1}}},
        {"科研 / 读研深造", "论文复现、原创研究与学术表达",
         {{"数学与统计基础", 3},
          {"机器学习", 3},
          {"深度学习", 3},
          {"论文阅读与复现", 3},
          {"Python 编程", 2},
          {"沟通与表达", 2}}},
    };
    return roles;
}

const Role* findRole(const std::string& name) {
    for (const auto& r : roleCatalog())
        if (r.name == name) return &r;
    return nullptr;
}

int requiredLevel(int weight) {
    if (weight >= 3) return 4;
    if (weight == 2) return 3;
    return 2;
}

// =============================================================================
//  五、业务逻辑：技能读写、匹配度、差距、路线
// =============================================================================

void ensureSkills(Profile& p) {
    if (!p.skills.empty()) return;
    for (const auto& name : skillCatalog()) p.skills.push_back(Skill{name, 0});
}

int skillLevel(const Profile& p, const std::string& name) {
    for (const auto& s : p.skills)
        if (s.name == name) return s.level;
    return 0;
}

void setSkillLevel(Profile& p, const std::string& name, int level) {
    level = std::max(0, std::min(5, level));
    for (auto& s : p.skills) {
        if (s.name == name) {
            s.level = level;
            return;
        }
    }
    p.skills.push_back(Skill{name, level});
}

// 加权匹配度：核心技能权重高；0~100
double roleMatch(const Profile& p, const Role& r) {
    int gained = 0, total = 0;
    for (const auto& n : r.needs) {
        gained += n.weight * skillLevel(p, n.skill);
        total += n.weight * 5;
    }
    if (total == 0) return 0.0;
    return 100.0 * gained / total;
}

std::vector<Gap> gapsOf(const Profile& p, const Role& r) {
    std::vector<Gap> out;
    for (const auto& n : r.needs) {
        const int cur = skillLevel(p, n.skill);
        const int need = requiredLevel(n.weight);
        if (cur < need) out.push_back(Gap{n.skill, cur, need, n.weight});
    }
    std::sort(out.begin(), out.end(), [](const Gap& a, const Gap& b) {
        if ((a.need - a.cur) != (b.need - b.cur)) return (a.need - a.cur) > (b.need - b.cur);
        return a.weight > b.weight;
    });
    return out;
}

std::string skillBar(int level, int maxLevel = 5) {
    const int v = std::max(0, std::min(maxLevel, level));
    return std::string(v, '#') + std::string(maxLevel - v, '.');
}

std::vector<RoadmapStage> buildRoadmap(const Profile& p, const Role& r) {
    std::vector<RoadmapStage> stages(4);
    stages[0] = RoadmapStage{"阶段一 · 基础夯实", 1, 6, {}};
    stages[1] = RoadmapStage{"阶段二 · 核心进阶", 7, 14, {}};
    stages[2] = RoadmapStage{"阶段三 · 项目实战", 15, 22, {}};
    stages[3] = RoadmapStage{"阶段四 · 求职冲刺", 23, 26, {}};

    std::vector<RoleNeed> needs = r.needs;
    std::sort(needs.begin(), needs.end(),
              [](const RoleNeed& a, const RoleNeed& b) { return a.weight > b.weight; });

    // 阶段一最多安排 4 项打基础，其余顺延到阶段二，避免 6 周内堆 7 门课
    int foundationCount = 0;
    for (const auto& n : needs) {
        const int cur = skillLevel(p, n.skill);
        const int need = requiredLevel(n.weight);
        if (cur >= need) continue;
        std::ostringstream os;
        if (cur <= 1 && foundationCount < 4) {
            os << "补齐「" << n.skill << "」：当前 " << cur << " 分 → 目标 " << need << " 分";
            stages[0].items.push_back(RoadmapItem{n.skill, os.str()});
            ++foundationCount;
        } else {
            os << (cur <= 1 ? "补齐「" : "进阶「") << n.skill << "」：当前 " << cur << " 分 → 目标 " << need << " 分";
            stages[1].items.push_back(RoadmapItem{n.skill, os.str()});
        }
    }

    int built = 0;
    for (const auto& n : needs) {
        if (built >= 3) break;
        stages[2].items.push_back(
            RoadmapItem{n.skill, "围绕「" + n.skill + "」做一个可展示项目（数据→训练→评估→报告/部署）"});
        ++built;
    }

    stages[3].items.push_back(RoadmapItem{"", "整理 1 页简历：突出 " + r.name + " 相关的 2 个项目与量化结果"});
    stages[3].items.push_back(RoadmapItem{"", "面试准备：ML 基础 8 题 + 项目深挖 5 问 + 手撕代码 20 题"});
    stages[3].items.push_back(RoadmapItem{"", "投递与复盘：每周投 5 家，记录面试问题并逐条补齐"});

    if (stages[0].items.empty() && stages[1].items.empty()) {
        stages[0].items.push_back(
            RoadmapItem{"", "当前技能已覆盖该岗位基础要求，直接进入项目实战，保持每周复盘"});
    }
    return stages;
}

const char* gradeName(int g) {
    static const char* names[] = {"大一", "大二", "大三", "大四", "研一", "研二", "研三"};
    if (g < 1 || g > 7) return "未知年级";
    return names[g - 1];
}

const char* projectStageName(int s) {
    static const char* names[] = {"构思中", "进行中", "已完成", "已上线"};
    if (s < 0 || s > 3) return "未知";
    return names[s];
}

const std::vector<std::string>& categoryCatalog() {
    static const std::vector<std::string> cats = {"课程", "项目", "竞赛", "实习", "科研", "求职", "其他"};
    return cats;
}

std::string numToStr(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << v;
    std::string s = os.str();
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.erase(s.size() - 2);
    return s;
}

int toInt(const std::string& s, int def) {
    try {
        size_t pos = 0;
        const int v = std::stoi(s, &pos);
        if (pos == s.size()) return v;
    } catch (...) {
    }
    return def;
}

double toDouble(const std::string& s, double def) {
    try {
        size_t pos = 0;
        const double v = std::stod(s, &pos);
        if (pos == s.size()) return v;
    } catch (...) {
    }
    return def;
}

// =============================================================================
//  六、持久化
// =============================================================================

bool saveData(const Workbench& wb) {
    std::ofstream out(wb.dataPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "DSH_WORKBENCH|1\n";
    out << "PROFILE|" << clean(wb.profile.name) << '|' << wb.profile.grade << '|' << wb.profile.weeklyHours
        << '|' << clean(wb.profile.targetRole) << '\n';
    for (const auto& s : wb.profile.skills) out << "SKILL|" << clean(s.name) << '|' << s.level << '\n';
    for (const auto& t : wb.tasks) {
        out << "TASK|" << t.id << '|' << clean(t.title) << '|' << clean(t.category) << '|' << t.due.y << '|'
            << t.due.m << '|' << t.due.d << '|' << (t.done ? 1 : 0) << '|' << numToStr(t.hours) << '\n';
    }
    for (const auto& p : wb.projects) {
        out << "PROJECT|" << p.id << '|' << clean(p.name) << '|' << clean(p.stack) << '|' << p.stage << '|'
            << clean(p.link) << '|' << clean(p.note) << '\n';
    }
    return static_cast<bool>(out);
}

bool loadData(Workbench& wb) {
    std::ifstream in(wb.dataPath, std::ios::binary);
    if (!in) return false;
    std::string line;
    if (!std::getline(in, line)) return false;
    if (line.rfind("DSH_WORKBENCH|", 0) != 0) return false;

    Workbench tmp;
    tmp.dataPath = wb.dataPath;
    tmp.profile.name = "同学";
    tmp.profile.grade = 3;
    tmp.profile.weeklyHours = 10;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto f = split(line, '|');
        if (f.empty()) continue;
        if (f[0] == "PROFILE" && f.size() >= 5) {
            tmp.profile.name = f[1];
            tmp.profile.grade = std::max(1, std::min(7, toInt(f[2], 3)));
            tmp.profile.weeklyHours = std::max(1, std::min(80, toInt(f[3], 10)));
            tmp.profile.targetRole = f[4];
        } else if (f[0] == "SKILL" && f.size() >= 3) {
            setSkillLevel(tmp.profile, f[1], toInt(f[2], 0));
        } else if (f[0] == "TASK" && f.size() >= 9) {
            Task t;
            t.id = toInt(f[1], 0);
            t.title = f[2];
            t.category = f[3];
            t.due = Date{toInt(f[4], 2026), toInt(f[5], 1), toInt(f[6], 1)};
            t.done = toInt(f[7], 0) != 0;
            t.hours = toDouble(f[8], 0);
            if (t.id > 0) tmp.tasks.push_back(t);
        } else if (f[0] == "PROJECT" && f.size() >= 7) {
            Project p;
            p.id = toInt(f[1], 0);
            p.name = f[2];
            p.stack = f[3];
            p.stage = std::max(0, std::min(3, toInt(f[4], 0)));
            p.link = f[5];
            p.note = f[6];
            if (p.id > 0) tmp.projects.push_back(p);
        }
    }

    ensureSkills(tmp.profile);
    for (const auto& t : tmp.tasks) tmp.nextTaskId = std::max(tmp.nextTaskId, t.id + 1);
    for (const auto& p : tmp.projects) tmp.nextProjectId = std::max(tmp.nextProjectId, p.id + 1);

    wb = tmp;
    return true;
}

void loadSampleData(Workbench& wb) {
    wb = Workbench{};
    wb.profile.name = "林晓";
    wb.profile.grade = 3;
    wb.profile.weeklyHours = 15;
    wb.profile.targetRole = "NLP / 大模型工程师";
    ensureSkills(wb.profile);
    setSkillLevel(wb.profile, "数学与统计基础", 2);
    setSkillLevel(wb.profile, "Python 编程", 3);
    setSkillLevel(wb.profile, "数据结构与算法", 2);
    setSkillLevel(wb.profile, "机器学习", 2);
    setSkillLevel(wb.profile, "深度学习", 1);
    setSkillLevel(wb.profile, "自然语言处理", 2);
    setSkillLevel(wb.profile, "大模型与提示工程", 2);
    setSkillLevel(wb.profile, "数据工程与 SQL", 1);
    setSkillLevel(wb.profile, "论文阅读与复现", 1);
    setSkillLevel(wb.profile, "产品与业务理解", 1);
    setSkillLevel(wb.profile, "沟通与表达", 2);

    const Date today = todayDate();
    wb.tasks = {
        {1, "完成《动手学深度学习》第 5-6 章并跑通示例", "课程", addDays(today, 10), false, 12},
        {2, "手撕 LeetCode 热题 100 中的 20 道", "求职", addDays(today, 21), false, 20},
        {3, "复现一篇 RAG 论文并写一篇技术博客", "科研", addDays(today, 30), false, 24},
        {4, "个人博客上线，发布项目复盘", "项目", addDays(today, -3), true, 6},
        {5, "参加校内 AI 创新赛初赛", "竞赛", addDays(today, 14), false, 16},
        {6, "整理简历第一版（含 2 个项目）", "求职", addDays(today, 40), false, 5},
    };
    wb.nextTaskId = 7;

    wb.projects = {
        {1, "中文医疗问答 RAG", "Python / LangChain / FAISS", 1, "github.com/linxiao/rag-medqa",
         "数据清洗完成，召回率待优化"},
        {2, "猫狗图像分类 baseline", "PyTorch / ResNet18", 2, "github.com/linxiao/cv-baseline", "验证集 94.2%"},
        {3, "校园课程助手 Agent", "Python / Function Calling", 0, "", "需求调研中"},
    };
    wb.nextProjectId = 4;
}

// =============================================================================
//  七、输入辅助
// =============================================================================

std::string promptLine(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) {
        g_inputClosed = true;
        std::cout << "\n";
        return "";
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return trim(line);
}

int promptInt(const std::string& prompt, int lo, int hi, int def) {
    while (true) {
        const std::string s = promptLine(prompt);
        if (g_inputClosed) return def;
        if (s.empty()) return def;
        try {
            size_t pos = 0;
            const int v = std::stoi(s, &pos);
            if (pos == s.size() && v >= lo && v <= hi) return v;
        } catch (...) {
        }
        std::cout << "  ! 请输入 " << lo << "~" << hi << " 之间的整数\n";
    }
}

double promptDouble(const std::string& prompt, double lo, double hi, double def) {
    while (true) {
        const std::string s = promptLine(prompt);
        if (g_inputClosed) return def;
        if (s.empty()) return def;
        try {
            size_t pos = 0;
            const double v = std::stod(s, &pos);
            if (pos == s.size() && v >= lo && v <= hi) return v;
        } catch (...) {
        }
        std::cout << "  ! 请输入 " << lo << "~" << hi << " 之间的数字\n";
    }
}

bool promptYesNo(const std::string& prompt, bool def) {
    const std::string s = promptLine(prompt);
    if (g_inputClosed || s.empty()) return def;
    if (s.compare(0, 3, "是") == 0) return true;
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    return c == 'y' || c == '1';
}

// =============================================================================
//  八、界面：头部、列表
// =============================================================================

void printHeader(const Workbench& wb) {
    const Role* role = findRole(wb.profile.targetRole);
    const int match = role ? static_cast<int>(std::lround(roleMatch(wb.profile, *role))) : 0;
    int total = 0, done = 0, overdue = 0;
    const long long today = toSerial(todayDate());
    for (const auto& t : wb.tasks) {
        ++total;
        if (t.done)
            ++done;
        else if (toSerial(t.due) < today)
            ++overdue;
    }
    std::cout << "\n";
    std::cout << std::string(79, '=') << "\n";
    std::cout << "        人工智能专业 · 职业生涯规划学习实践工作台    AI Career Workbench v1.0\n";
    std::cout << std::string(79, '=') << "\n";
    std::cout << " " << wb.profile.name << " · " << gradeName(wb.profile.grade) << " · 每周可投入 "
              << wb.profile.weeklyHours << "h  |  目标岗位："
              << (wb.profile.targetRole.empty() ? "未设置" : wb.profile.targetRole) << "  |  匹配度 " << match
              << "%\n";
    std::cout << " 任务 " << done << "/" << total << " 完成 (" << percentOf(done, total) << "%) · 逾期 " << overdue
              << " · 实践项目 " << wb.projects.size() << " 个\n";
    std::cout << std::string(79, '-') << "\n";
}

void printMainMenu() {
    std::cout << " 1. 个人档案与技能自评        5. 实践项目台账\n";
    std::cout << " 2. 岗位图谱与匹配分析        6. 数据看板\n";
    std::cout << " 3. 学习路线与阶段任务        7. 数据管理（保存/读取/示例/清空）\n";
    std::cout << " 4. 任务管理                  0. 保存并退出\n";
}

std::vector<const Task*> sortedTasks(const Workbench& wb, int filter) {
    // filter: 0 全部 1 待办 2 已完成 3 逾期
    const long long today = toSerial(todayDate());
    std::vector<const Task*> list;
    for (const auto& t : wb.tasks) {
        const bool overdue = !t.done && toSerial(t.due) < today;
        bool keep = true;
        if (filter == 1) keep = !t.done;
        if (filter == 2) keep = t.done;
        if (filter == 3) keep = overdue;
        if (keep) list.push_back(&t);
    }
    std::sort(list.begin(), list.end(), [today](const Task* a, const Task* b) {
        if (a->done != b->done) return !a->done;
        if (toSerial(a->due) != toSerial(b->due)) return toSerial(a->due) < toSerial(b->due);
        return a->id < b->id;
    });
    return list;
}

void printTaskTable(const Workbench& wb, int filter) {
    const auto list = sortedTasks(wb, filter);
    if (list.empty()) {
        std::cout << "  （没有符合条件的任务）\n";
        return;
    }
    const long long today = toSerial(todayDate());
    std::cout << "  " << padLeft("ID", 4) << "  " << padRight("状态", 6) << "  " << padRight("任务", 34) << "  "
              << padRight("分类", 8) << "  " << padRight("截止", 15) << "  " << padRight("剩余", 12) << "工时\n";
    for (const Task* t : list) {
        const bool overdue = !t->done && toSerial(t->due) < today;
        const std::string status = t->done ? "完成" : (overdue ? "逾期" : "待办");
        const std::string remain = t->done ? "已完成" : remainText(daysUntil(t->due));
        std::cout << "  " << padLeft(std::to_string(t->id), 4) << "  " << padRight(status, 6) << "  "
                  << padRight(clipTo(t->title, 34), 34) << "  " << padRight(clipTo(t->category, 8), 8) << "  "
                  << padRight(dateStrWithWeek(t->due), 15) << "  " << padRight(remain, 12) << numToStr(t->hours)
                  << "h\n";
    }
}

void printProjectTable(const Workbench& wb) {
    if (wb.projects.empty()) {
        std::cout << "  （还没有记录实践项目）\n";
        return;
    }
    std::cout << "  " << padLeft("ID", 4) << "  " << padRight("项目", 30) << "  " << padRight("技术栈", 26) << "  "
              << padRight("阶段", 10) << "链接\n";
    for (const auto& p : wb.projects) {
        std::cout << "  " << padLeft(std::to_string(p.id), 4) << "  " << padRight(clipTo(p.name, 30), 30) << "  "
                  << padRight(clipTo(p.stack, 26), 26) << "  " << padRight(projectStageName(p.stage), 10)
                  << clipTo(p.link, 34) << "\n";
    }
}

// =============================================================================
//  九、功能：个人档案
// =============================================================================

void editBasicInfo(Profile& p) {
    const std::string name = promptLine("  姓名 [" + p.name + "]: ");
    if (!name.empty()) p.name = name;

    std::cout << "  年级：1 大一  2 大二  3 大三  4 大四  5 研一  6 研二  7 研三\n";
    p.grade = promptInt("  年级 [" + std::to_string(p.grade) + "]: ", 1, 7, p.grade);
    p.weeklyHours = promptInt("  每周可投入小时 [" + std::to_string(p.weeklyHours) + "]: ", 1, 80, p.weeklyHours);

    std::cout << "  目标岗位：\n";
    const auto& roles = roleCatalog();
    for (size_t i = 0; i < roles.size(); ++i)
        std::cout << "   " << (i + 1) << ". " << roles[i].name << "\n";
    const int cur = [&]() {
        for (size_t i = 0; i < roles.size(); ++i)
            if (roles[i].name == p.targetRole) return static_cast<int>(i) + 1;
        return 0;
    }();
    const int pick = promptInt("  选择编号 [" + std::to_string(cur) + "]: ", 0, static_cast<int>(roles.size()), cur);
    if (pick >= 1) p.targetRole = roles[static_cast<size_t>(pick) - 1].name;
}

void menuProfile(Workbench& wb) {
    while (true) {
        std::cout << "\n【个人档案与技能自评】\n";
        std::cout << "  姓名：" << wb.profile.name << "   年级：" << gradeName(wb.profile.grade)
                  << "   每周投入：" << wb.profile.weeklyHours << "h   目标岗位："
                  << (wb.profile.targetRole.empty() ? "未设置" : wb.profile.targetRole) << "\n";
        std::cout << "  自评说明：0 未接触 · 1 了解概念 · 2 跟教程做过 · 3 独立完成课程项目 · 4 完整项目/实习 · 5 可指导他人\n";
        int skillNo = 0;
        for (const auto& s : wb.profile.skills) {
            ++skillNo;
            std::cout << "   " << padLeft(std::to_string(skillNo), 2) << ". " << padRight(s.name, 20) << "["
                      << skillBar(s.level) << "] " << s.level << "\n";
        }
        std::cout << "  1. 修改基本信息   2. 逐项技能自评   3. 修改单项技能   0. 返回\n";
        const int c = promptInt("  请选择: ", 0, 3, 0);
        if (g_inputClosed || c == 0) return;
        if (c == 1) {
            editBasicInfo(wb.profile);
            saveData(wb);
        } else if (c == 2) {
            std::cout << "  按回车保留当前分数。\n";
            for (auto& s : wb.profile.skills) {
                s.level = promptInt("  " + padRight(s.name, 20) + " [" + std::to_string(s.level) + "]: ", 0, 5, s.level);
            }
            saveData(wb);
            std::cout << "  已更新技能自评。\n";
        } else if (c == 3) {
            std::cout << "  技能编号见上方列表（1 起）。\n";
            const int idx = promptInt("  技能编号: ", 1, static_cast<int>(wb.profile.skills.size()), 1);
            if (idx >= 1 && idx <= static_cast<int>(wb.profile.skills.size())) {
                auto& s = wb.profile.skills[static_cast<size_t>(idx) - 1];
                s.level = promptInt("  " + s.name + " 新分数: ", 0, 5, s.level);
                saveData(wb);
            }
        }
    }
}

// =============================================================================
//  十、功能：岗位图谱与匹配
// =============================================================================

void showRoleDetail(const Profile& p, const Role& r) {
    std::cout << "\n  " << r.name << "\n  " << r.summary << "\n";
    std::cout << "  匹配度：" << static_cast<int>(std::lround(roleMatch(p, r))) << "%\n";
    std::cout << "  " << padRight("技能", 20) << "  " << padRight("权重", 8) << "  " << padRight("你的水平", 12)
              << "  " << padRight("建议目标", 10) << "差距\n";
    for (const auto& n : r.needs) {
        const int cur = skillLevel(p, n.skill);
        const int need = requiredLevel(n.weight);
        std::string gapText = cur >= need ? "已达标" : ("缺 " + std::to_string(need - cur) + " 分");
        std::cout << "  " << padRight(n.skill, 20) << "  "
                  << padRight(n.weight >= 3 ? "核心" : (n.weight == 2 ? "重要" : "加分"), 8) << "  "
                  << padRight(skillBar(cur) + " " + std::to_string(cur), 12) << "  " << padRight(std::to_string(need), 10)
                  << gapText << "\n";
    }
}

void menuRoles(Workbench& wb) {
    const auto& roles = roleCatalog();
    while (true) {
        std::cout << "\n【岗位图谱与匹配分析】  （匹配度 = 岗位所需技能加权得分 / 满分）\n";
        std::vector<std::pair<double, int>> ranked;
        for (size_t i = 0; i < roles.size(); ++i) ranked.push_back({roleMatch(wb.profile, roles[i]), static_cast<int>(i)});
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::cout << "  " << padLeft("排名", 6) << "  " << padRight("岗位", 30) << "  " << padRight("匹配度", 10)
                  << "主要差距\n";
        for (size_t k = 0; k < ranked.size(); ++k) {
            const Role& r = roles[static_cast<size_t>(ranked[k].second)];
            const auto gaps = gapsOf(wb.profile, r);
            std::string gapText = gaps.empty() ? "已达标" : ("缺 " + gaps.front().skill);
            if (gaps.size() > 1) gapText += " 等 " + std::to_string(gaps.size()) + " 项";
            const bool target = (r.name == wb.profile.targetRole);
            const std::string shownName = r.name + (target ? " *" : "");
            std::cout << "  " << padLeft(std::to_string(k + 1), 6) << "  " << padRight(shownName, 30) << "  "
                      << padRight(std::to_string(static_cast<int>(std::lround(ranked[k].first))) + "%", 10) << gapText
                      << "\n";
        }
        std::cout << "  * 为当前目标岗位。  1. 查看岗位详情   2. 设为目标岗位   0. 返回\n";
        const int c = promptInt("  请选择: ", 0, 2, 0);
        if (g_inputClosed || c == 0) return;
        if (c == 1 || c == 2) {
            const int idx = promptInt("  岗位编号（按上表排名，1 起）: ", 1, static_cast<int>(roles.size()), 1);
            if (idx < 1) continue;
            const int roleIndex = ranked[static_cast<size_t>(idx) - 1].second;
            const Role& r = roles[static_cast<size_t>(roleIndex)];
            if (c == 1) {
                showRoleDetail(wb.profile, r);
            } else {
                wb.profile.targetRole = r.name;
                saveData(wb);
                std::cout << "  目标岗位已设为：" << r.name << "，可到菜单 3 生成学习路线。\n";
            }
        }
    }
}

// =============================================================================
//  十一、功能：学习路线
// =============================================================================

void menuRoadmap(Workbench& wb) {
    while (true) {
        const Role* role = findRole(wb.profile.targetRole);
        if (!role) {
            std::cout << "  还没有目标岗位，请先在菜单 1 或 2 中设置。\n";
            return;
        }
        const auto stages = buildRoadmap(wb.profile, *role);
        std::cout << "\n【学习路线 · " << role->name << "】  共 26 周，按每周 " << wb.profile.weeklyHours
                  << " 小时设计\n";
        for (const auto& st : stages) {
            std::cout << "\n  " << st.title << "（第 " << st.startWeek << "-" << st.endWeek << " 周）\n";
            if (st.items.empty()) {
                std::cout << "    · 本阶段无硬性差距，可巩固已学内容或提前进入项目\n";
                continue;
            }
            for (const auto& it : st.items) std::cout << "    · " << it.text << "\n";
        }
        std::cout << "\n  1. 一键生成本路线任务（带截止日期）   0. 返回\n";
        const int c = promptInt("  请选择: ", 0, 1, 0);
        if (g_inputClosed || c == 0) return;
        if (c != 1) continue;

        const Date today = todayDate();
        int added = 0;
        for (size_t si = 0; si < stages.size(); ++si) {
            for (const auto& it : stages[si].items) {
                const std::string category = (si <= 1) ? "课程" : (si == 2 ? "项目" : "求职");
                // 去重：完全同名，或同一技能在同一分类下已有任务（技能分数变化后重生成不重复）
                bool exists = false;
                for (const auto& t : wb.tasks) {
                    if (t.title == it.text) exists = true;
                    if (!it.skill.empty() && t.category == category &&
                        t.title.find("「" + it.skill + "」") != std::string::npos)
                        exists = true;
                }
                if (exists) continue;
                Task t;
                t.id = wb.nextTaskId++;
                t.title = it.text;
                t.category = category;
                t.due = addDays(today, stages[si].endWeek * 7);
                t.done = false;
                t.hours = (si <= 1) ? 18.0 : (si == 2 ? 36.0 : 6.0);
                wb.tasks.push_back(t);
                ++added;
            }
        }
        saveData(wb);
        std::cout << "  已生成 " << added << " 条任务" << (added == 0 ? "（路线内容均已在任务清单中）" : "") << "。\n";
    }
}

// =============================================================================
//  十二、功能：任务管理
// =============================================================================

void addTask(Workbench& wb) {
    Task t;
    t.id = wb.nextTaskId;
    t.title = promptLine("  任务标题: ");
    if (t.title.empty()) {
        std::cout << "  已取消（标题为空）。\n";
        return;
    }
    std::cout << "  分类：";
    const auto& cats = categoryCatalog();
    for (size_t i = 0; i < cats.size(); ++i) std::cout << (i + 1) << " " << cats[i] << "  ";
    std::cout << "\n";
    const int ci = promptInt("  选择分类 [1]: ", 1, static_cast<int>(cats.size()), 1);
    t.category = cats[static_cast<size_t>(ci) - 1];

    std::cout << "  截止日期格式：2026-12-31 或 12-31（默认 30 天后）\n";
    Date due = addDays(todayDate(), 30);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string s = promptLine("  截止日期 [" + dateStr(due) + "]: ");
        if (s.empty()) break;
        if (parseDate(s, due)) break;
        std::cout << "  ! 日期格式不正确，例如 2026-12-31\n";
    }
    t.due = due;
    t.hours = promptDouble("  预估工时(小时) [8]: ", 0.5, 500, 8);
    t.done = false;
    wb.tasks.push_back(t);
    ++wb.nextTaskId;
    saveData(wb);
    std::cout << "  已添加任务 #" << t.id << "。\n";
}

Task* findTask(Workbench& wb, int id) {
    for (auto& t : wb.tasks)
        if (t.id == id) return &t;
    return nullptr;
}

void menuTasks(Workbench& wb) {
    int filter = 0;
    while (true) {
        const long long today = toSerial(todayDate());
        int done = 0, overdue = 0;
        for (const auto& t : wb.tasks) {
            if (t.done)
                ++done;
            else if (toSerial(t.due) < today)
                ++overdue;
        }
        static const char* filterName[4] = {"全部", "待办", "已完成", "逾期"};
        std::cout << "\n【任务管理】 " << wb.tasks.size() << " 项 · 已完成 " << done << " · 逾期 " << overdue
                  << "  |  当前筛选：" << filterName[filter] << "\n";
        printTaskTable(wb, filter);
        std::cout << "  1. 新增任务  2. 标记完成  3. 撤回完成  4. 删除任务  5. 切换筛选  0. 返回\n";
        const int c = promptInt("  请选择: ", 0, 5, 0);
        if (g_inputClosed || c == 0) return;
        if (c == 1) {
            addTask(wb);
        } else if (c == 2 || c == 3) {
            const int id = promptInt("  任务 ID: ", 1, 1000000, -1);
            Task* t = findTask(wb, id);
            if (!t) {
                std::cout << "  未找到该任务。\n";
                continue;
            }
            t->done = (c == 2);
            saveData(wb);
            std::cout << "  任务 #" << id << (c == 2 ? " 已标记完成。" : " 已撤回为待办。") << "\n";
        } else if (c == 4) {
            const int id = promptInt("  要删除的任务 ID: ", 1, 1000000, -1);
            auto it = std::find_if(wb.tasks.begin(), wb.tasks.end(), [id](const Task& t) { return t.id == id; });
            if (it == wb.tasks.end()) {
                std::cout << "  未找到该任务。\n";
                continue;
            }
            if (promptYesNo("  确认删除「" + clipTo(it->title, 24) + "」? (y/N): ", false)) {
                wb.tasks.erase(it);
                saveData(wb);
                std::cout << "  已删除。\n";
            }
        } else if (c == 5) {
            std::cout << "  1 全部  2 待办  3 已完成  4 逾期\n";
            filter = promptInt("  筛选: ", 1, 4, 1) - 1;
        }
    }
}

// =============================================================================
//  十三、功能：实践项目台账
// =============================================================================

void addProject(Workbench& wb) {
    Project p;
    p.id = wb.nextProjectId;
    p.name = promptLine("  项目名称: ");
    if (p.name.empty()) {
        std::cout << "  已取消（名称为空）。\n";
        return;
    }
    p.stack = promptLine("  技术栈（如 PyTorch / FAISS / FastAPI）: ");
    p.link = promptLine("  仓库或演示链接（可空）: ");
    p.note = promptLine("  复盘备注（可空）: ");
    p.stage = promptInt("  阶段 (0 构思 / 1 进行中 / 2 已完成 / 3 已上线): ", 0, 3, 0);
    wb.projects.push_back(p);
    ++wb.nextProjectId;
    saveData(wb);
    std::cout << "  已添加项目 #" << p.id << "。\n";
}

void menuProjects(Workbench& wb) {
    while (true) {
        std::cout << "\n【实践项目台账】 共 " << wb.projects.size() << " 个\n";
        printProjectTable(wb);
        std::cout << "  1. 新增项目  2. 更新阶段  3. 编辑链接/备注  4. 删除项目  0. 返回\n";
        const int c = promptInt("  请选择: ", 0, 4, 0);
        if (g_inputClosed || c == 0) return;
        if (c == 1) {
            addProject(wb);
            continue;
        }
        if (wb.projects.empty()) {
            std::cout << "  还没有项目可操作。\n";
            continue;
        }
        const int id = promptInt("  项目 ID: ", 1, 1000000, -1);
        auto it = std::find_if(wb.projects.begin(), wb.projects.end(), [id](const Project& p) { return p.id == id; });
        if (it == wb.projects.end()) {
            std::cout << "  未找到该项目。\n";
            continue;
        }
        bool removed = false;
        if (c == 2) {
            it->stage = promptInt("  新阶段 (0 构思 / 1 进行中 / 2 已完成 / 3 已上线): ", 0, 3, it->stage);
        } else if (c == 3) {
            const std::string link = promptLine("  链接 [" + it->link + "]: ");
            if (!link.empty()) it->link = link;
            const std::string note = promptLine("  复盘备注 [" + it->note + "]: ");
            if (!note.empty()) it->note = note;
        } else if (c == 4) {
            if (!promptYesNo("  确认删除「" + clipTo(it->name, 24) + "」? (y/N): ", false)) continue;
            wb.projects.erase(it);
            removed = true;
        }
        saveData(wb);
        std::cout << (removed ? "  已删除。\n" : "  已更新。\n");
    }
}

// =============================================================================
//  十四、功能：数据看板
// =============================================================================

void menuDashboard(const Workbench& wb) {
    const long long today = toSerial(todayDate());
    int total = 0, done = 0, overdue = 0;
    double plannedHours = 0, doneHours = 0;
    std::vector<int> catTotal(categoryCatalog().size(), 0);
    std::vector<int> catDone(categoryCatalog().size(), 0);
    for (const auto& t : wb.tasks) {
        ++total;
        plannedHours += t.hours;
        if (t.done) {
            ++done;
            doneHours += t.hours;
        } else if (toSerial(t.due) < today) {
            ++overdue;
        }
        for (size_t i = 0; i < categoryCatalog().size(); ++i) {
            if (t.category == categoryCatalog()[i]) {
                ++catTotal[i];
                if (t.done) ++catDone[i];
            }
        }
    }

    std::cout << "\n【数据看板】 " << dateStrWithWeek(todayDate()) << "\n";
    std::cout << std::string(79, '-') << "\n";
    std::cout << " 一、任务进度\n";
    std::cout << "   总数 " << total << "  |  已完成 " << done << " (" << percentOf(done, total) << "%)  |  逾期 "
              << overdue << "  |  计划工时 " << numToStr(plannedHours) << "h / 已完成 " << numToStr(doneHours)
              << "h\n";

    std::cout << "\n 二、分类推进\n";
    for (size_t i = 0; i < categoryCatalog().size(); ++i) {
        if (catTotal[i] == 0) continue;
        const int pct = percentOf(catDone[i], catTotal[i]);
        const int cells = pct / 5;
        std::cout << "   " << padRight(categoryCatalog()[i], 8) << "[" << std::string(static_cast<size_t>(cells), '#')
                  << std::string(static_cast<size_t>(20 - cells), '.') << "] " << padLeft(std::to_string(pct), 3)
                  << "%  (" << catDone[i] << "/" << catTotal[i] << ")\n";
    }

    std::cout << "\n 三、技能画像\n";
    const Role* role = findRole(wb.profile.targetRole);
    if (role) {
        std::cout << "   目标岗位：" << role->name << "   匹配度 " << static_cast<int>(std::lround(roleMatch(wb.profile, *role)))
                  << "%\n";
        for (const auto& n : role->needs) {
            const int cur = skillLevel(wb.profile, n.skill);
            const int need = requiredLevel(n.weight);
            std::cout << "   " << padRight(n.skill, 20) << "[" << skillBar(cur) << "] " << cur << " / 建议 " << need
                      << (cur >= need ? "  ✓" : "") << "\n";
        }
    } else {
        std::cout << "   尚未设置目标岗位。\n";
    }

    std::cout << "\n 四、项目阶段分布\n   ";
    int stageCount[4] = {0, 0, 0, 0};
    for (const auto& p : wb.projects)
        if (p.stage >= 0 && p.stage <= 3) ++stageCount[p.stage];
    for (int i = 0; i < 4; ++i) std::cout << projectStageName(i) << " " << stageCount[i] << "   ";
    std::cout << "\n";

    std::cout << "\n 五、接下来做什么\n";
    const auto todo = sortedTasks(wb, 1);
    if (todo.empty()) {
        std::cout << "   待办已清空，建议补充新的实践项目或更新技能自评。\n";
    } else {
        int shown = 0;
        for (const Task* t : todo) {
            if (shown >= 3) break;
            std::cout << "   · " << clipTo(t->title, 40) << "（" << remainText(daysUntil(t->due)) << "）\n";
            ++shown;
        }
        if (overdue > 0) std::cout << "   注意：有 " << overdue << " 项已逾期，先处理或重排截止日期。\n";
    }
    std::cout << "\n 按回车返回...";
    promptLine("");
}

// =============================================================================
//  十五、功能：数据管理
// =============================================================================

void menuData(Workbench& wb) {
    while (true) {
        std::cout << "\n【数据管理】 当前存档：" << wb.dataPath << "\n";
        std::cout << "  1. 保存   2. 重新读取   3. 载入示例数据   4. 清空任务与项目   0. 返回\n";
        const int c = promptInt("  请选择: ", 0, 4, 0);
        if (g_inputClosed || c == 0) return;
        if (c == 1) {
            std::cout << (saveData(wb) ? "  保存成功。\n" : "  保存失败，请检查目录写权限。\n");
        } else if (c == 2) {
            if (!loadData(wb)) {
                std::cout << "  读取失败：存档不存在或格式不符。\n";
            } else {
                std::cout << "  已读取存档：" << wb.tasks.size() << " 项任务，" << wb.projects.size() << " 个项目。\n";
            }
        } else if (c == 3) {
            if (promptYesNo("  载入示例会覆盖当前数据，继续? (y/N): ", false)) {
                loadSampleData(wb);
                saveData(wb);
                std::cout << "  已载入示例数据（林晓，大三，目标 NLP / 大模型工程师）。\n";
            }
        } else if (c == 4) {
            if (promptYesNo("  确认清空全部任务与项目? (y/N): ", false)) {
                wb.tasks.clear();
                wb.projects.clear();
                wb.nextTaskId = 1;
                wb.nextProjectId = 1;
                saveData(wb);
                std::cout << "  已清空任务与项目（个人档案与技能保留）。\n";
            }
        }
    }
}

// =============================================================================
//  十六、首次使用向导
// =============================================================================

void setupWizard(Workbench& wb) {
    std::cout << "\n" << std::string(79, '=') << "\n";
    std::cout << "  欢迎使用「人工智能专业 · 职业生涯规划学习实践工作台」\n";
    std::cout << "  三步完成初始化：基本信息 → 目标岗位 → 技能自评（可跳过，之后在菜单 1 补填）\n";
    std::cout << std::string(79, '=') << "\n";
    const std::string name = promptLine("  你的名字 [同学]: ");
    if (!name.empty()) wb.profile.name = name;

    std::cout << "  年级：1 大一  2 大二  3 大三  4 大四  5 研一  6 研二  7 研三\n";
    wb.profile.grade = promptInt("  年级 [3]: ", 1, 7, 3);
    wb.profile.weeklyHours = promptInt("  每周可投入学习时间(小时) [10]: ", 1, 80, 10);

    std::cout << "\n  选择目标岗位（不确定可先选，之后随时可改）：\n";
    const auto& roles = roleCatalog();
    for (size_t i = 0; i < roles.size(); ++i) std::cout << "   " << (i + 1) << ". " << roles[i].name << "\n";
    const int pick = promptInt("  编号 [3]: ", 1, static_cast<int>(roles.size()), 3);
    wb.profile.targetRole = roles[static_cast<size_t>(pick) - 1].name;

    ensureSkills(wb.profile);
    std::cout << "\n  是否现在逐项技能自评（13 项，约 1 分钟）? (y/N): ";
    if (promptYesNo("", false)) {
        std::cout << "  0 未接触 · 1 了解概念 · 2 跟教程做过 · 3 独立完成课程项目 · 4 完整项目/实习 · 5 可指导他人\n";
        for (auto& s : wb.profile.skills) {
            s.level = promptInt("  " + padRight(s.name, 20) + " [0]: ", 0, 5, 0);
        }
    } else {
        std::cout << "  已跳过，默认 0 分；可随时在菜单 1 中补填。\n";
    }

    saveData(wb);
    std::cout << "\n  初始化完成，数据保存在：" << wb.dataPath << "\n";
}

// =============================================================================
//  十七、入口
// =============================================================================

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::ios::sync_with_stdio(false);

    Workbench wb;
    ensureSkills(wb.profile);

    std::ifstream probe(wb.dataPath, std::ios::binary);
    const bool hasData = probe.good() && probe.peek() != std::ifstream::traits_type::eof();
    probe.close();

    if (!hasData) {
        setupWizard(wb);
    } else if (!loadData(wb)) {
        std::cout << "\n  存档格式无法识别，进入初始化向导。\n";
        setupWizard(wb);
    }

    while (!g_inputClosed) {
        printHeader(wb);
        printMainMenu();
        const int choice = promptInt(" 请选择: ", 0, 7, 0);
        if (g_inputClosed) break;
        switch (choice) {
            case 0:
                if (saveData(wb)) std::cout << "\n  已保存到 " << wb.dataPath << "。\n";
                std::cout << "  坚持记录、按周复盘，祝拿到心仪的 offer。再见！\n";
                return 0;
            case 1: menuProfile(wb); break;
            case 2: menuRoles(wb); break;
            case 3: menuRoadmap(wb); break;
            case 4: menuTasks(wb); break;
            case 5: menuProjects(wb); break;
            case 6: menuDashboard(wb); break;
            case 7: menuData(wb); break;
            default: break;
        }
    }

    saveData(wb);
    return 0;
}
