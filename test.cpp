#include "finetree.h"
#include "mcts.h"
#include <string>
#include <memory>
#include <iostream>
#include <pgnparser.h>
#include <array>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace
{
using clock_type = std::chrono::steady_clock;

class sink_io final : public io_handler
{
public:
    std::string read_line() override { return {}; }
    void write_line(const std::string &) override {}
    bool is_open() override { return false; }
};

struct protocol_position
{
    std::string initial_position;
    std::string moves;
    std::string pgn;
};

double seconds_since(clock_type::time_point started)
{
    return std::chrono::duration<double>(
        clock_type::now() - started).count();
}

protocol_position load_protocol_position(const std::string &path)
{
    std::ifstream input(path);
    if(!input)
    {
        throw std::runtime_error("cannot open protocol log: " + path);
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string text = contents.str();

    constexpr std::string_view position_marker = "> position ";
    const std::size_t position_pos = text.rfind(position_marker);
    if(position_pos == std::string::npos)
    {
        throw std::runtime_error("protocol log has no UCI position");
    }
    const std::size_t go_pos =
        text.find(" | > go movetime", position_pos);
    if(go_pos == std::string::npos)
    {
        throw std::runtime_error(
            "protocol log has no go after its final position");
    }
    const std::string position_command = text.substr(
        position_pos + position_marker.size(),
        go_pos - position_pos - position_marker.size());
    const std::size_t moves_pos = position_command.find(" moves ");
    if(moves_pos == std::string::npos)
    {
        throw std::runtime_error(
            "final UCI position has no moves component");
    }

    constexpr std::string_view pgn_marker = "Partial game PGN:\n";
    const std::size_t pgn_pos = text.rfind(pgn_marker);
    if(pgn_pos == std::string::npos)
    {
        throw std::runtime_error("protocol log has no partial PGN");
    }

    return {
        .initial_position = position_command.substr(0, moves_pos),
        .moves = position_command.substr(moves_pos + 7),
        .pgn = text.substr(pgn_pos + pgn_marker.size())
    };
}
}

std::string pgn = R"(
[Board "Standard - Turn Zero"]

1. (0T1)g2g4 / (0T1)Ng8h6 
2. (0T2)f2f4 / (0T2)Nh6>>(0T1)f6 
3. (-1T2)Bf1g2 (0T3)e2e3 / (-1T2)Nb8c6 
4. (-1T3)Bg2f3 / (0T3)Rh8g8 (-1T3)Ra8b8 
5. (-1T4)h2h3 (0T4)Ke1f2 / (0T4)Rg8h8 (-1T4)Ng8h6 
6. (-1T5)Nb1c3 (0T5)a2a4 / (0T5)Rh8g8 (-1T5)g7g5 
7. (-1T6)a2a4 (0T6)Bf1g2 / (0T6)Rg8h8 (-1T6)Rh8g8 
8. (-1T7)e2e3 (0T7)Bg2e4 / (0T7)Rh8g8 (-1T7)Rg8h8 
9. (-1T8)a4a5 (0T8)b2b4 / (0T8)Rg8h8 (-1T8)Bf8g7 
10. (-1T9)Bf3xc6 (0T9)Ra1a3 / (0T9)Rh8g8 (-1T9)e7e6 
11. (-1T10)Bc6e4 (0T10)Be4d3 / (0T10)Rg8h8 (-1T10)Ke8f8 
12. (-1T11)Ra1a2 (0T11)Bd3f1 / (0T11)Rh8g8 (-1T11)Kf8e8 
13. (-1T12)Be4xb7 (0T12)Qd1e1 / (0T12)Rg8h8 (-1T12)Qd8e7 
14. (-1T13)Ng1e2 (0T13)c2c4 / (0T13)Rh8g8 (-1T13)Rh8g8 
15. (-1T14)Bb7a6 (0T14)e3e4 / (0T14)Rg8h8 (-1T14)Ke8d8 
16. (-1T15)d2d3 (0T15)h2h3 / (0T15)Rh8g8 (-1T15)Rg8h8 
17. (-1T16)Ne2f4 (0T16)Ra3e3 / (0T16)Rg8h8 (-1T16)Rh8g8 
18. (-1T17)Ra2a4 (0T17)Ng1f3 / (0T17)Rh8g8 (-1T17)Rg8h8 
19. (-1T18)Ra4c4 (0T18)Bc1a3 / (0T18)Rg8h8 (-1T18)Rh8g8 
20. (-1T19)Nc3a2 (0T19)Qe1d1 / (0T19)Rh8g8 (-1T19)Rg8h8 
21. (-1T20)Ke1d2 (0T20)a4a5 / (0T20)Rg8h8 (-1T20)Rh8e8 
22. (-1T21)Rc4b4 (0T21)Rh1g1 / (0T21)Rh8g8 (-1T21)Re8h8 
23. (-1T22)Rb4c4 (0T22)g4g5 / (0T22)Rg8h8 (-1T22)Rh8g8 
24. (-1T23)Kd2e2 (0T23)Bf1g2 / (0T23)Rh8g8 (-1T23)Kd8e8 
25. (-1T24)Rc4c6 (0T24)Rg1e1 / (0T24)Rg8h8 (-1T24)Ke8d8 
26. (-1T25)Na2c3 (0T25)Kf2g1 / (0T25)Rh8g8 (-1T25)Rg8h8 
27. (-1T26)d3d4 (0T26)Re1e2 / (0T26)Rg8h8 (-1T26)Rh8g8 
28. (-1T27)b2b4 (0T27)h3h4 / (0T27)Rh8g8 (-1T27)Rg8e8 
29. (-1T28)Bc1d2 (0T28)Re3c3 / (0T28)Rg8h8 (-1T28)Re8g8 
30. (-1T29)Qd1b1 (0T29)Rc3c1 / (0T29)Rh8g8 (-1T29)Rg8e8 
31. (-1T30)Nf4d3 (0T30)Nf3h2 / (0T30)Rg8h8 (-1T30)Bc8b7 
32. (-1T31)Rh1e1 (0T31)Nh2f1 / (0T31)Rh8g8 (-1T31)Kd8c8 
33. (-1T32)Ke2d1 (0T32)Nf1g3 / (0T32)Rg8h8 (-1T32)Re8g8 
34. (-1T33)Nc3e4 (0T33)Qd1a4 / (0T33)Rh8g8 (-1T33)Rg8h8 
35. (-1T34)Nd3b2 (0T34)Kg1h2 / (0T34)Rg8h8 (-1T34)Rh8g8 
36. (-1T35)Bd2c3 (0T35)Bg2f3 / (0T35)Rh8g8 (-1T35)Rg8f8 
37. (-1T36)Ba6b5 (0T36)Nb1c3 / (0T36)Rg8h8 (-1T36)Rf8g8 
38. (-1T37)a5a6 (0T37)Qa4d1 / (0T37)Rh8g8 (-1T37)Rg8d8 
39. (-1T38)a6xb7+ (0T38)Re2e3 / (0T38)Nb8c6 (-1T38)Kc8>>(-1T37)d8 
40. (-2T38)Bb5f1 (-1T39)f2f3 (0T39)Bf3h5 / (0T39)Rg8h8 (-1T39)Rb8a8 
41. (-1T40)Ne4d6 (0T40)Bh5g4 / (0T40)Rh8g8 (-1T40)Rd8f8 
42. (-1T41)b7xa8 (0T41)Qd1f1 / (0T41)Rg8h8 (-1T41)Rf8e8 
43. (-1T42)Nd6b7 (0T42)Qf1h1 / (0T42)Rh8g8 (-1T42)Re8g8 
44. (-1T43)Nb7d6 (0T43)a5a6 / (0T43)Rg8h8 (-1T43)Rg8h8 
45. (-1T44)Bb5a4 (0T44)Nc3b5 / (0T44)Rh8g8 (-1T44)Rh8b8 
46. (-1T45)Re1g1 (0T45)Qh1g1 / (0T45)Rg8h8 (-1T45)Rb8g8 
47. (-1T46)Nd6xf7 (0T46)Bg4e6 / (0T46)Rh8g8 (-1T46)Rg8e8 
48. (-1T47)Rc6d6 (0T47)Re3d3 / (0T47)Rg8h8 (-1T47)Re8h8 
49. (-1T48)Qa8d5 (0T48)Qg1g2 / (0T48)Rh8g8 (-1T48)Rh8d8 
50. (-1T49)Kd1c1 (0T49)Ng3f1 / (0T49)Rg8h8 (-1T49)Rd8h8 
51. (-1T50)e3e4 (0T50)Be6d5 / (0T50)Rh8g8 (-1T50)Rh8e8 
52. (-1T51)Rg1d1 (0T51)Rd3h3 / (0T51)Rg8h8 (-1T51)Re8g8 
53. (-1T52)Rd1f1 (0T52)Qg2f2 / (0T52)Rh8g8 (-1T52)Rg8h8 
54. (-1T53)Qb1a2 (0T53)Qf2d4 / (0T53)Rg8h8 (-1T53)Rh8e8 
55. (-1T54)Rf1h1 (0T54)Rh3c3 / (0T54)Rh8g8 (-1T54)Re8g8 
56. (-1T55)Qd5xg5 (0T55)Qd4c5 / (0T55)Rg8h8 (-1T55)Rg8c8 
57. (-1T56)Nb2c4 (0T56)Rc1e1 / (0T56)Rh8g8 (-1T56)Rc8f8 
58. (-1T57)Qg5d2 (0T57)d2d4 / (0T57)Rg8h8 (-1T57)Rf8h8 
59. (-1T58)h3h4 (0T58)Qc5xc6 / (0T58)Rh8g8 (-1T58)Rh8c8 
60. (-1T59)Qd2f2 (0T59)a6xb7 / (0T59)Rg8h8 (-1T59)Rc8h8 
61. (-1T60)Qf2g2 (0T60)Nf1d2 / (0T60)Rh8g8 (-1T60)Rh8f8 
62. (-1T61)Qg2e2 (0T61)Qc6d6 / (0T61)Rg8h8 (-1T61)Rf8h8 
63. (-1T62)Ba4xd7 (0T62)Qd6h6 / (0T62)Rh8g8 (-1T62)Rh8a8 
64. (-1T63)Nc4e3 (0T63)Qh6h5 / (0T63)Rg8h8 (-1T63)Ra8c8 
65. (-1T64)Rh1h3 (0T64)Rc3c2 / (0T64)Rh8g8 (-1T64)Rc8h8 
66. (-1T65)Rd6c6 (0T65)Nb5xa7 / (0T65)Rg8h8 (-1T65)Rh8g8 
67. (-1T66)d4d5 (0T66)Rc2a2 / (0T66)Rh8g8 (-1T66)Rg8f8 
68. (-1T67)Qa2c4 (0T67)Qh5xf7+ / (-1T67)Qe7>x(0T67)f7 (-2T38)Rg8f8 
69. (-2T39)Rc6xe6 (-1T68)Kc1b1 (0T68)b7xc8 / (0T68)Rg8h8 (-1T68)Rf8g8 
70. (-1T69)Qe2f1 (0T69)Nd2b1 / (0T69)Rh8g8 (-1T69)Rg8h8 
71. (-1T70)Rc6c5 (0T70)Ra2d2 / (0T70)Rg8h8 (-1T70)Rh8c8 
72. (-1T71)Nf7d8 (0T71)Rd2a2 / (0T71)Rh8g8 (-1T71)Rc8xd8 
73. (-1T72)Ne3g2 (0T72)Ba3b2 / (0T72)Rg8h8 (-1T72)Rd8f8 
74. (-1T73)Qc4d4 (0T73)Qc8xd7+ / (0T73)Ke8xd7 (-1T73)Rf8h8 
75. (-1T74)Qd4e5+ (0T74)Kh2g1 / (0T74)Qd8>x(-1T74)d7 (-2T39)Rf8>>(-2T38)f8 
76. (-3T39)Ne4xg5 (-2T40)d4d5 (-1T75)Rc5b5 (0T75)Bd5b7 / (0T75)Rh8g8 (-1T75)Rh8f8 
77. (-1T76)Bc3e1 (0T76)Bb2a3 / (0T76)Rg8h8 (-1T76)Rf8g8 
78. (-1T77)Qe5g5 (0T77)c4c5 / (0T77)Rh8g8 (-1T77)Rg8h8 
79. (-1T78)Qg5g6 (0T78)Bb7a6 / (0T78)Rg8h8 (-1T78)Rh8e8 
80. (-1T79)e4e5 (0T79)Kg1f2 / (0T79)Rh8g8 (-1T79)Re8b8 
81. (-1T80)Qg6xf6 (0T80)Kf2e3 / (0T80)Rg8h8 (-1T80)Rb8h8 
82. (-1T81)c2c3 (0T81)Nb1d2 / (0T81)Rh8g8 (-1T81)Rh8a8 
83. (-1T82)Qf1h1 (0T82)Nd2b1 / (0T82)Rg8h8 (-1T82)Ra8h8 
84. (-1T83)Be1d2 (0T83)Nb1c3 / (0T83)Rh8g8 (-1T83)Rh8g8 
85. (-1T84)c3c4 (0T84)Ra2e2 / (0T84)Rg8h8 (-1T84)Rg8c8 
86. (-1T85)Qf6g6 (0T85)Re1c1 / (0T85)Rh8g8 (-1T85)Bg7h8 
87. (-1T86)Qg6e8+ (0T86)Rc1f1 / (-1T86)Rc8xe8 (0T86)Rg8>>(-1T86)g8 (-3T39)Rg8>>(-2T39)g8 (-2T40)Kd8>>(-3T39)e8 
88. (-6T40)Nb2c4 (-5T40)d4d5 (-4T87)Qe8f7 (-3T40)Qb1a1 (-2T41)Ne4d6+ (-1T87)Qh1d1 (0T87)Rf1e1 / (0T87)Ra8e8 (-1T87)Re8c8 
89. (-1T88)Qd1h1 (0T88)Ba6c8+ / (0T88)Re8xc8 (-1T88)Bh8g7 
90. (-1T89)Rb5b7 (0T89)Re1a1 / (0T89)Rc8e8 (-1T89)Rc8h8 
91. (-1T90)Ng2e1 (0T90)Nc3b5 / (0T90)Re8d8 (-1T90)Rh8f8 
92. (-1T91)Rb7xc7 (0T91)Ra1c1 / (0T91)Rd8e8 (-1T91)Rf8d8 
93. (-1T92)Rc7c6 (0T92)Nb5d6+ / (0T92)e7xd6 (-1T92)Rd8e8 
94. (-1T93)Kb1a1 (0T93)d4d5 / (0T93)Bf8e7 (-1T93)Re8h8 
95. (-1T94)Ka1b1 (0T94)h4h5 / (0T94)Re8h8 (-1T94)Rh8g8 
96. (-1T95)d5xe6 (0T95)f4f5 / (0T95)Rh8g8 (-1T95)Rg8h8 
97. (-1T96)Bd2xh6 (0T96)Ke3f4 / (0T96)Rg8h8 (-1T96)Rh8g8 
98. (-1T97)Ne1g2 (0T97)f5f6 / (0T97)Rh8g8 (-1T97)Rg8d8 
99. (-1T98)Rc6c7 (0T98)Ba3b2 / (0T98)Rg8h8 (-1T98)Rd8g8 
100. (-1T99)Bh6f4 (0T99)Rc1d1 / (0T99)Rh8g8 (-1T99)Rg8f8 
101. (-1T100)Rc7xd7 (0T100)Rd1b1 / (0T100)Kd7e8 (-1T100)Rf8g8 
102. (-1T101)Qh1h2 (0T101)Bb2d4 / (0T101)Rg8h8 (-1T101)Rg8h8 
103. (-1T102)b4b5 (0T102)Rb1b2 / (0T102)Rh8g8 (-1T102)Rh8f8 
104. (-1T103)Rd7d5 (0T103)Re2h2 / (0T103)Rg8h8 (-1T103)Rf8h8 
105. (-1T104)b5b6 (0T104)Rh2g2 / (0T104)Rh8g8 (-1T104)Rh8f8 
106. (-1T105)Qh2g1 (0T105)Na7c6 / (0T105)Rg8h8 (-1T105)Rf8h8 
107. (-1T106)Bf4e3 (0T106)e4e5 / (0T106)Rh8g8 (-1T106)Rh8g8 
108. (-1T107)Rd5b5 (0T107)Rb2f2 / (0T107)Rg8h8 (-1T107)Rg8c8 
109. (-1T108)Be3h6 (0T108)Rg2g1 / (0T108)Rh8g8 (-1T108)Rc8h8 
110. (-1T109)Rh3h2 (0T109)Nc6b8 / (0T109)Rg8h8 (-1T109)Rh8e8 
111. (-1T110)Qg1e1 (0T110)Rg1g4 / (0T110)Rh8g8 (-1T110)Re8h8 
112. (-1T111)Bh6e3 (0T111)Rf2g2 / (0T111)Rg8h8 (-1T111)Rh8g8 
113. (-1T112)Be3d2 (0T112)e5e6 / (0T112)Rh8g8 (-1T112)Rg8h8 
114. (-1T113)Bd2c1 (0T113)Kf4f3 / (0T113)Rg8h8 (-1T113)Rh8g8 
115. (-1T114)Qe1g1 (0T114)c5c6 / (0T114)Rh8g8 (-1T114)Rg8e8 
116. (-1T115)Qg1h1 (0T115)Bd4e3 / (0T115)Rg8h8 (-1T115)Re8g8 
117. (-1T116)Bc1a3 (0T116)b4b5 / (0T116)Rh8g8 (-1T116)Rg8h8 
118. (-1T117)Ba3c5 (0T117)Rg2c2 / (0T117)Rg8h8 (-1T117)Rh8g8 
119. (-1T118)Rb5b2 (0T118)Rc2c1 / (0T118)Rh8g8 (-1T118)Rg8a8 
120. (-1T119)Rb2b4 (0T119)Rc1e1 / (0T119)Rg8h8 (-1T119)Ra8h8 
121. (-1T120)Ng2f4 (0T120)Re1b1 / (0T120)Rh8g8 (-1T120)Rh8g8 
122. (-1T121)e6e7 (0T121)Kf3f4 / (0T121)Rg8h8 (-1T121)Rg8h8 
123. (-1T122)Kb1a1 (0T122)Kf4f5 / (0T122)Rh8g8 (-1T122)Rh8g8 
124. (-1T123)Qh1f1 (0T123)Be3b6 / (0T123)Rg8h8 (-1T123)Rg8h8 
125. (-1T124)Rh2h3 (0T124)Nb8a6 / (0T124)Rh8g8 (-1T124)Rh8g8 
126. (-1T125)h4h5 (0T125)Bb6a5 / (0T125)Rg8h8 (-1T125)Rg8d8 
127. (-1T126)Rb4b5 (0T126)b5b6 / (0T126)Rh8g8 (-1T126)Rd8b8 
128. (-1T127)Qf1e1 (0T127)b6b7 / (0T127)Rg8h8 (-1T127)Rb8d8 
129. (-1T128)Qe1c1 (0T128)Rg4a4 / (0T128)Rh8g8 (-1T128)Rd8h8 
130. (-1T129)Rh3g3 (0T129)Rb1g1 / (0T129)Rg8h8 (-1T129)Rh8e8 
131. (-1T130)h5h6 (0T130)Na6b8 / (0T130)Rh8g8 (-1T130)Re8h8 
132. (-1T131)Rb5b4 (0T131)Ra4f4 / (0T131)Rg8h8 (-1T131)Rh8b8 
133. (-1T132)Qc1d1 (0T132)Rg1f1 / (0T132)Rh8g8 (-1T132)Rb8h8 
134. (-1T133)Qd1c2 (0T133)e6xf7+ / (0T133)Ke8f8 (-1T133)Rh8f8 
135. (-1T134)Qc2d2 (0T134)Rf4e4 / (0T134)Rg8h8 (-1T134)Rf8a8 
136. (-1T135)Qd2d4 (0T135)Re4e3 / (0T135)Rh8g8 (-1T135)Ra8h8 
137. (-1T136)g4g5 (0T136)Rf1f3 / (0T136)Rg8h8 (-1T136)Rh8g8 
138. (-1T137)Nf4e6 (0T137)Re3xe7 / (0T137)Rh8g8 (-1T137)Rg8h8 
139. (-1T138)Qd4d7 (0T138)Kf5e6 / (0T138)Rg8h8 (-1T138)Bg7xh6 
140. (-1T139)b6xa7 (0T139)Rf3h3 / (0T139)Rh8g8 (-1T139)Rh8g8 
141. (-1T140)e7e8+ (0T140)g5g6 / (-1T140)Rg8xe8 (-6T40)Rg8h8 (-5T40)Bg7>>(-6T40)g6 (-3T40)Nh6>>(-5T40)g6 (0T140)Kf8>>(-1T139)g8 
142. (-9T140)Rg3g2 (-8T41)Kd1e2 (-7T41)b4b5 (-6T41)d4d5 (-3T41)a6xb7+ (-1T141)Ne6c7 (0T141)f7f8+ / (0T141)Rg8xf8 (-1T141)Re8g8 
143. (-5T41)Re6xf6 (-1T142)Qd7d3 (0T142)Rh3g3 / (0T142)Rf8h8 (-1T142)Rg8h8 
144. (-1T143)Qd3d4 (0T143)Rg3d3 / (0T143)Rh8g8 (-1T143)Rh8g8 
145. (-1T144)Qd4d5 (0T144)Rd3c3 / (0T144)Rg8h8 (-1T144)Rg8h8 
146. (-1T145)Rb4b7 (0T145)g6xh7 / (0T145)Rh8g8 (-1T145)Rh8g8 
147. (-1T146)Bc5g1 (0T146)Rc3h3 / (0T146)Rg8h8 (-1T146)Rg8h8 
148. (-1T147)Rg3g2 (0T147)Ba5e1 / (0T147)Rh8g8 (-1T147)Rh8g8 
149. (-1T148)Nc7b5 (0T148)Be1b4 / (0T148)Rg8h8 (-1T148)Rg8h8 
150. (-1T149)Ka1b1 (0T149)Bb4a5 / (0T149)Rh8g8 (-1T149)Rh8g8 
151. (-1T150)g5xh6 (0T150)Rh3a3 / (0T150)Rg8h8 (-1T150)Rg8h8 
152. (-1T151)Kb1a2 (0T151)f6xg7 / (0T151)Rh8g8 (-1T151)Rh8g8 
153. (-1T152)Rb7f7 (0T152)h7h8 / (0T152)Rg8xh8 (-1T152)Rg8f8 
154. (-1T153)e5e6 (0T153)Ra3d3 / (0T153)Rh8g8 (-1T153)Rf8h8 
155. (-1T154)Qd5f5 (0T154)Ke6d7 / (0T154)Rg8h8 (-1T154)Rh8g8 
156. (-1T155)e6e7 (0T155)Rd3d1 / (0T155)Rh8g8 (-1T155)Rg8h8 
157. (-1T156)Rg2e2 (0T156)Re7e2 / (0T156)Rg8h8 (-1T156)Rh8g8 
158. (-1T157)Ka2b1 (0T157)Re2e1 / (0T157)Rh8g8 (-1T157)Rg8h8 
159. (-1T158)Bg1c5 (0T158)Kd7xc7 / (0T158)Rg8h8 (-1T158)Rh8f8 
160. (-1T159)Qf5e5 (0T159)Rd1b1 / (0T159)Rh8g8 (-1T159)Rf8h8 
161. (-1T160)Rf7xh7 (0T160)Nb8d7 / (0T160)Rg8h8 (-1T160)Rh8g8 
162. (-1T161)Rh7g7 (0T161)Rb1a1 / (0T161)Rh8g8 (-1T161)Rg8h8 
163. (-1T162)Qe5h2 (0T162)Ra1a4 / (0T162)Rg8h8 (-1T162)Rh8g8 
164. (-1T163)Bc5d6 (0T163)Ra4a2 / (0T163)Rh8g8 (-1T163)Rg8h8 
165. (-1T164)Rg7g4 (0T164)Nd7b6 / (0T164)Rg8h8 (-1T164)Rh8e8 
166. (-1T165)Qh2g2 (0T165)Re1c1 / (0T165)Rh8g8 (-1T165)Re8g8 
167. (-1T166)Rg4d4 (0T166)Ba5e1 / (0T166)Rg8h8 (-1T166)Rg8h8 
168. (-1T167)Rd4g4 (0T167)Ra2c2 / (0T167)Rh8g8 (-1T167)Rh8e8 
169. (-1T168)Nb5c7 (0T168)Be1d2 / (0T168)Rg8h8 (-1T168)Re8g8 
170. (-1T169)Re2e1 (0T169)Rc1h1 / (0T169)Rh8g8 (-1T169)Rg8f8 
171. (-1T170)Bd6b4 (0T170)Rh1d1 / (0T170)Rg8h8 (-1T170)Rf8h8 
172. (-1T171)Qg2b2 (0T171)Rd1h1 / (0T171)Rh8g8 (-1T171)Rh8g8 
173. (-1T172)Re1e6 (0T172)Bd2e1 / (0T172)Rg8h8 (-1T172)Rg8h8 
174. (-1T173)Re6g6 (0T173)Rc2g2 / (0T173)Rh8g8 (-1T173)Rh8g8 
175. (-1T174)f3f4 (0T174)Be1a5 / (0T174)Rg8h8 (-1T174)Rg8h8 
176. (-1T175)Qb2d2 (0T175)Rh1h4 / (0T175)Rh8g8 (-1T175)Rh8g8 
177. (-1T176)Qd2f2 (0T176)Rg2d2 / (0T176)Rg8h8 (-1T176)Rg8h8 
178. (-1T177)Bb4d6 (0T177)Rd2e2 / (0T177)Rh8g8 (-1T177)Rh8f8 
179. (-1T178)Rg6g5 (0T178)Kc7xd6 / (0T178)Rg8h8 (-1T178)Rf8h8 
180. (-1T179)Rg5g6 (0T179)Rh4g4 / (0T179)Rh8g8 (-1T179)Rh8g8 
181. (-1T180)Qf2g3 (0T180)Ba5c3 / (0T180)Rg8h8 (-1T180)Rg8d8 
182. (-1T181)Rg6e6 (0T181)Bc3d4 / (0T181)Rh8g8 (-1T181)Rd8g8 
183. (-1T182)f4f5 (0T182)Re2b2 / (0T182)Rg8h8 (-1T182)Rg8h8 
184. (-1T183)Qg3e5 (0T183)Bd4f2 / (0T183)Rh8g8 (-1T183)Rh8d8 
185. (-1T184)Rg4g5 (0T184)Kd6e7 / (0T184)Rg8h8 (-1T184)Rd8g8 
186. (-1T185)Rg5xg8 (0T185)Rg4c4 / (0T185)Rh8>(-1T185)h8 (-9T140)Kg8f7 (-8T41)Ng6>>(-6T40)g6 (-7T41)Ke8>>(-8T41)e8 (-6T41)Rh8>>(-8T41)h8 (-5T41)Qe7>>x(-8T41)b4 (-2T41)Kc8>>(-1T40)b8 
187. (-14T41)Nb2a4+ (-13T42)Ke2d3 (-12T42)f2f4 (-11T42)Ke2d3 (-10T41)Nc4b6+ (-9T141)Qd7d8 (-8T42)d5d6 (-7T42)Nc4a3 (-6T42)d5xe6 (-5T42)Rf6e6 (-2T42)Qb1a2 (-1T186)Rg8e8 (0T186)Bf2d4 / (-1T186)Rh8>(0T186)h8 
188. (-1T187)Re6g6 (0T187)Ke7d6 / (0T187)Rh8>(-1T187)h8 
189. (-1T188)Kb1c2 (0T188)Nb6a8 / (-1T188)Rh8>(0T188)h8 
190. (-1T189)Rg6g5 (0T189)Bd4c3 / (0T189)Rh8>(-1T189)h8 
191. (-1T190)Nc7e6 (0T190)b7b8 / (-1T190)Rh8>(0T190)h8 
192. (-1T191)Bd6b4 (0T191)Rc4c5
)";


fine_tree_pruning_policy parse_policy(const std::string &name)
{
    if(name == "current_hc")
        return fine_tree_pruning_policy::current_hc;
    if(name == "current_cell")
        return fine_tree_pruning_policy::current_cell;
    if(name == "current_node")
        return fine_tree_pruning_policy::current_node;
    if(name == "descendant_subtree")
        return fine_tree_pruning_policy::descendant_subtree;
    if(name == "ancestor_nodes")
        return fine_tree_pruning_policy::ancestor_nodes;
    if(name == "ancestor_fanout")
        return fine_tree_pruning_policy::ancestor_fanout;
    throw std::runtime_error("unknown pruning policy: " + name);
}

int main(int argc, char **argv)
{
    using fn = fine_node<std::monostate>;
    constexpr std::array<index_t, 28> expected_witness{
        1, 10,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    if(argc < 3 || argc > 4)
    {
        std::cerr << "usage: " << argv[0]
                  << " <current_hc|current_cell|current_node|"
                     "descendant_subtree|ancestor_nodes|ancestor_fanout]"
                     " <max-codim> [protocol-log]\n";
        return 2;
    }

    const std::string policy_name = argv[1];
    const index_t max_codim =
        static_cast<index_t>(std::stoul(argv[2]));
    constexpr size_t disjoint_weight = 2;
    const bool loaded_from_file = argc == 4;
    std::string pgn_text = pgn;
    std::string position_name = "hardcoded";

    const fine_tree_pruning_policy policy = parse_policy(policy_name);
    fine_tree_options options{
        .pruning_policy = policy,
        .scan_policy = {
            .disjoint_weight = disjoint_weight
        },
        .quality_policy = {
            .max_codim = max_codim
        }
    };

    if(loaded_from_file)
    {
        position_name = argv[3];
        protocol_position input =
            load_protocol_position(position_name);
        pgn_text = std::move(input.pgn);

        mcts_engine mcts(std::make_unique<sink_io>(), options);
        auto started = clock_type::now();
        mcts.set_position(input.initial_position, input.moves);
        std::cout << "UCI set_position " << seconds_since(started)
                  << " s\n" << std::flush;

        started = clock_type::now();
        auto best = mcts.find_best_move(std::nullopt, 1000, {});
        std::cout << "MCTS movetime 1000 " << seconds_since(started)
                  << " s, result=" << (best ? "move" : "none")
                  << '\n' << std::flush;
    }

    std::cout << "policy: " << policy_name
              << "; disjoint weight: " << disjoint_weight
              << "; max codim: " << max_codim
              << "; position: " << position_name << '\n';

    const auto parse_started = clock_type::now();
    state s(*pgnparser(pgn_text).parse_game());
    std::cout << "parse " << seconds_since(parse_started) << " s\n";
    std::unique_ptr<fn> root =
        fn::make_root(s, std::monostate{}, options);

    // Materialize the first complete witness path, as root::is_terminal()
    // does in the first MCTS iteration.
    const auto initial_started = std::chrono::steady_clock::now();
    auto root_index = root->search().first();
    const auto initial_elapsed =
        std::chrono::steady_clock::now() - initial_started;
    std::cout << "policy " << policy_name
              << " initial search completed in "
              << std::chrono::duration<double>(initial_elapsed).count()
              << " s; result=";
    if(root_index)
    {
        std::cout << *root_index << '\n';
    }
    else
    {
        std::cout << "none\n";
    }
    if(!root_index)
    {
        std::cout << "position has no initial witness\n";
        return 0;
    }
    if(!loaded_from_file
       && *root_index != expected_witness.front())
    {
        throw std::runtime_error("unexpected root witness");
    }

    fn *first_axis_node = root->get_child(*root_index);
    if(first_axis_node == nullptr)
    {
        throw std::runtime_error("root witness child was not materialized");
    }

    fn *ceiling = nullptr;
    if(loaded_from_file)
    {
        ceiling = first_axis_node->get_nearby_ceiling();
        if(ceiling == nullptr)
        {
            throw std::runtime_error(
                "initial witness has no ceiling");
        }
    }
    else
    {
        ceiling = first_axis_node;
        std::size_t depth = 0;
        while(true)
        {
            if(depth >= expected_witness.size()
               || ceiling->get_n() != static_cast<index_t>(depth)
               || ceiling->get_i() != expected_witness[depth])
            {
                throw std::runtime_error(
                    "fine-tree witness differs at depth "
                    + std::to_string(depth));
            }
            if(ceiling->is_ceiling())
            {
                break;
            }

            const auto children = ceiling->get_children();
            if(children.size() != 1)
            {
                throw std::runtime_error(
                    "expected a single-child witness path");
            }
            ceiling = children.front();
            depth++;
        }
        if(depth + 1 != expected_witness.size())
        {
            throw std::runtime_error(
                "fine-tree witness has an unexpected length");
        }
    }

    // The first rollout ignites the witness ceiling. The slow operation is the
    // later request for another child below the fixed axis-0 coordinate.
    ceiling->ignite();
    std::cout << "expanding axis 1..." << std::endl;
    const auto started = std::chrono::steady_clock::now();
    auto next_child = first_axis_node->search().first();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    std::cout << "policy " << policy_name
              << " post-ignite search completed in "
              << std::chrono::duration<double>(elapsed).count()
              << " s; result=";
    if(next_child)
    {
        std::cout << *next_child;
    }
    else
    {
        std::cout << "none";
    }
    std::cout << '\n';

    if(next_child)
    {
        fn *result_node = first_axis_node->get_child(*next_child);
        if(result_node == nullptr)
        {
            throw std::runtime_error(
                "returned child was not materialized");
        }
        fn *result_ceiling = result_node->get_nearby_ceiling();
        if(result_ceiling == nullptr)
        {
            throw std::runtime_error(
                "returned child has no ceiling witness");
        }

        auto *result_context = first_axis_node->get_context();
        point result_point(result_context->hc_info.dimension);
        fn *cursor = result_ceiling;
        for(index_t axis_count = 0;
            axis_count < result_context->hc_info.dimension;
            axis_count++)
        {
            result_point[cursor->get_n()] = cursor->get_i();
            cursor = cursor->get_parent();
            if(cursor == nullptr)
            {
                throw std::runtime_error(
                    "ceiling witness ended before the nodal ancestor");
            }
        }

        HC validation_hc = result_context->hc_info.universe;
        if(result_context->hc_info.find_problem(
               result_point, validation_hc))
        {
            throw std::runtime_error(
                "returned ceiling witness still contains a problem");
        }
        std::cout << "find_problem recheck: valid\n";
    }
    return 0;
}
