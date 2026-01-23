#include "action.h"
#include <regex>
#include <sstream>
#include "utils.h"
#include "state.h"


full_move::full_move(std::string str): from{0,0,0,0}, to{0,0,0,0}
{
    std::regex pattern1(R"(\((-?\d+)T(-?\d+)\)[A-Z]?([a-h])([1-8])([a-h])([1-8]))");
    std::regex pattern3(R"(\((-?\d+)T(-?\d+)\)[A-Z]?([a-h])([1-8])>?>?\((-?\d+)T(-?\d+)\)([a-h])([1-8]))");
    std::smatch match;
    int l1, t1, x1, y1;
    int l2, t2, x2, y2;
    
    if(std::regex_match(str, match, pattern1))
    {
        l1 = l2 = std::stoi(match[1]);
        t1 = t2 = std::stoi(match[2]);
        x1 = static_cast<int>(match[3].str()[0] - 'a');
        y1 = static_cast<int>(match[4].str()[0] - '1');
        x2 = static_cast<int>(match[5].str()[0] - 'a');
        y2 = static_cast<int>(match[6].str()[0] - '1');
    }
    else if(std::regex_match(str, match, pattern3))
    {
        l1 = std::stoi(match[1]);
        t1 = std::stoi(match[2]);
        x1 = static_cast<int>(match[3].str()[0] - 'a');
        y1 = static_cast<int>(match[4].str()[0] - '1');
        l2 = std::stoi(match[5]);
        t2 = std::stoi(match[6]);
        x2 = static_cast<int>(match[7].str()[0] - 'a');
        y2 = static_cast<int>(match[8].str()[0] - '1');
    }
    else
    {
        throw std::runtime_error("Cannot match this move in any known pattern: " + str);
    }
    from = vec4(x1, y1, t1, l1);
    to = vec4(x2, y2, t2, l2);
}

std::string full_move::to_string() const
{
    std::ostringstream os;
    vec4 p = from, q = to;
    vec4 d = q - p;
    if(d.t() == 0 && d.l() == 0)
    {
        os << '(' << p.l() << 'T' << p.t() << ')' << (char)(p.x()+'a') << (char)(p.y()+'1') << (char)(q.x()+'a') << (char)(q.y()+'1');
    }
    else
    {
        os << '(' << p.l() << 'T' << p.t() << ')' << (char)(p.x()+'a') << (char)(p.y()+'1') << "(" << q.l() << 'T' << q.t() << ')' << (char)(q.x()+'a') << (char)(q.y()+'1');
    }
    return os.str();
}

bool full_move::operator<(const full_move &other) const
{
    return std::tie(from, to) < std::tie(other.from, other.to); 
}

bool full_move::operator==(const full_move &other) const
{
    return std::tie(from, to) == std::tie(other.from, other.to);
}

std::ostream &operator<<(std::ostream &os, const full_move &fm)
{
    os << fm.to_string();
    return os;
}

/*********************************/

void action::sort(std::vector<ext_move> &mvs, const state &s)
{
    size_t branching_index = 0;
    auto [present, player] = s.get_present();
    std::set<int> moved_lines;
    for(size_t i=0; i<mvs.size(); i++)
    {
        vec4 p = mvs[i].fm.from;
        vec4 q = mvs[i].fm.to;
        auto tc1 = std::make_pair(q.t(), player);
        auto tc2 = s.get_timeline_end(q.l());
        bool branching =  tc1 < tc2 || (tc1 == tc2 && moved_lines.contains(q.l()));
        moved_lines.insert(p.l());
        if(branching)
        {
            std::swap(mvs[i], mvs[branching_index]);
            branching_index++;
        }
        else
        {
            moved_lines.insert(q.l());
        }
    }
    if(branching_index < mvs.size())
    {
        int sign = player ? -1 : 1;
        std::sort(mvs.begin()+branching_index, mvs.end(), [sign](ext_move m1, ext_move m2){
            return sign*m1.get_to().l() < sign*m2.get_to().l();
        });
        std::rotate(mvs.begin(), mvs.begin()+branching_index, mvs.end());
    }
}

action action::from_vector(const std::vector<ext_move> &mvs, const state &s)
{
    action a{mvs};
    sort(a.mvs, s);
    return a;
}

std::vector<ext_move> action::get_moves() const
{
    return mvs;
}

std::ostream& operator<<(std::ostream &os, const action &act)
{
    for(const auto &mv : act.mvs)
    {
        os << mv.to_string() << " ";
    }
    return os;
}
