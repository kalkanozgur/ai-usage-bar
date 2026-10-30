#include "core/report_html.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>

#include "core/time_util.hpp"

namespace cub::report {
namespace {

constexpr std::int64_t kDay = 86400;
constexpr std::int64_t kGapSec = 20 * 60;  // longer gaps (widget hidden) break the line
constexpr std::size_t kMaxHoverPoints = 480;  // points embedded for the hover crosshair

// ---- formatting -------------------------------------------------------------

std::string fixed(double v, int precision) {
    std::array<char, 40> buf{};
    const auto r =
        std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, precision);
    std::string s(buf.data(), r.ptr);
    std::replace(s.begin(), s.end(), '.', ',');  // Turkish decimal comma
    return s;
}

std::string svg_num(double v) {
    std::array<char, 40> buf{};
    const auto r =
        std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, 1);
    return std::string(buf.data(), r.ptr);
}

struct Local {
    time::CivilDate date;
    int hour = 0;
    int minute = 0;
    int weekday = 0;  ///< 0 = Sunday
};

Local to_local(std::int64_t ts, std::int32_t off) {
    const std::int64_t t = ts + off;
    std::int64_t days = t / kDay;
    std::int64_t rem = t % kDay;
    if (rem < 0) {
        rem += kDay;
        --days;
    }
    Local l;
    l.date = time::civil_from_days(days);
    l.hour = static_cast<int>(rem / 3600);
    l.minute = static_cast<int>(rem / 60 % 60);
    l.weekday = static_cast<int>(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday (4)
    return l;
}

std::string two(int v) {
    std::string s = std::to_string(v);
    return v < 10 ? "0" + s : s;
}

std::string fmt_hm(std::int64_t ts, std::int32_t off) {
    const Local l = to_local(ts, off);
    return two(l.hour) + ":" + two(l.minute);
}

std::string fmt_dm(std::int64_t ts, std::int32_t off) {
    const Local l = to_local(ts, off);
    return two(static_cast<int>(l.date.day)) + "." + two(static_cast<int>(l.date.month));
}

std::string fmt_dt(std::int64_t ts, std::int32_t off) {
    return fmt_dm(ts, off) + " " + fmt_hm(ts, off);
}

std::string fmt_wday(std::int64_t ts, std::int32_t off) {
    static constexpr std::array<const char*, 7> kNames = {"Paz", "Pzt", "Sal", "\xC3\x87" "ar",
                                                          "Per", "Cum", "Cmt"};
    const Local l = to_local(ts, off);
    return std::string(kNames[static_cast<std::size_t>(l.weekday)]) + " " + fmt_dm(ts, off);
}

std::int64_t local_midnight(std::int64_t ts, std::int32_t off) {
    std::int64_t t = ts + off;
    std::int64_t d = t / kDay;
    if (t % kDay < 0) {
        --d;
    }
    return d * kDay - off;
}

// ---- views ------------------------------------------------------------------

struct Range {
    View view;
    const char* id;
    const char* label;
    std::int64_t from;
    std::int64_t tick;  ///< x-axis tick spacing (seconds)
};

std::vector<const breakdown::Result*> sessions_in(const Input& in, const Range& r) {
    std::vector<const breakdown::Result*> out;
    if (r.view == View::LastSession) {
        const breakdown::Result* best = nullptr;
        for (const auto& s : in.sessions) {
            const std::int64_t end = s.ended_at.value_or(0);
            if (best == nullptr || end > best->ended_at.value_or(0)) {
                best = &s;
            }
        }
        if (best != nullptr) {
            out.push_back(best);
        }
        return out;
    }
    for (const auto& s : in.sessions) {
        if (s.ended_at && *s.ended_at >= r.from) {
            out.push_back(&s);
        }
    }
    std::sort(out.begin(), out.end(), [](const auto* a, const auto* b) {
        return a->ended_at.value_or(0) > b->ended_at.value_or(0);
    });
    return out;
}

// ---- line chart -------------------------------------------------------------

constexpr double kW = 720.0;
constexpr double kH = 240.0;
constexpr double kL = 44.0;   // left: y labels
constexpr double kR = 88.0;   // right: direct labels ("Oturum %100")
constexpr double kT = 12.0;
constexpr double kB = 26.0;   // bottom: x labels

struct Series {
    const char* name;
    const char* css_var;
    bool session;
};

void line_chart(std::string& o, const Input& in, const Range& r) {
    const std::int64_t from = r.from;
    const std::int64_t to = std::max(in.now, from + 60);
    const double pw = kW - kL - kR;
    const double ph = kH - kT - kB;
    auto x = [&](std::int64_t t) {
        return kL + pw * static_cast<double>(t - from) / static_cast<double>(to - from);
    };
    auto y = [&](double p) { return kT + ph * (1.0 - std::clamp(p, 0.0, 100.0) / 100.0); };

    std::vector<const usage::Sample*> pts;
    for (const auto& s : in.samples) {
        if (s.ts >= from && s.ts <= to) {
            pts.push_back(&s);
        }
    }

    o += "<figure class=\"chart\"><figcaption>Limit kullan\xC4\xB1m\xC4\xB1 (%)</figcaption>";
    if (pts.empty()) {
        o += "<p class=\"empty\">Bu aral\xC4\xB1kta limit \xC3\xB6rne\xC4\x9Fi yok. Widget her "
             "ba\xC5\x9F" "ar\xC4\xB1l\xC4\xB1 sorguda bir \xC3\xB6rnek kaydeder.</p></figure>";
        return;
    }
    // Hover data: [ts, session, week] (null when missing), downsampled; read by the inline script.
    std::string pj = "[";
    {
        const std::size_t step = std::max<std::size_t>(1, pts.size() / kMaxHoverPoints);
        auto val = [](const std::optional<double>& v) {
            if (!v) {
                return std::string("null");
            }
            std::array<char, 32> b{};
            const auto res =
                std::to_chars(b.data(), b.data() + b.size(), *v, std::chars_format::fixed, 1);
            return std::string(b.data(), res.ptr);
        };
        for (std::size_t i = 0; i < pts.size(); i += step) {
            if (pj.size() > 1) {
                pj += ',';
            }
            pj += "[" + std::to_string(pts[i]->ts) + "," + val(pts[i]->session_pct) + "," +
                  val(pts[i]->week_pct) + "]";
        }
        pj += "]";
    }
    o += "<svg class=\"lc\" viewBox=\"0 0 720 240\" role=\"img\" aria-label=\"Oturum ve hafta "
         "y\xC3\xBCzdesi\" data-from=\"" +
         std::to_string(from) + "\" data-to=\"" + std::to_string(to) + "\" data-off=\"" +
         std::to_string(in.utc_offset_sec) + "\" data-l=\"" + svg_num(kL) + "\" data-pw=\"" +
         svg_num(pw) + "\" data-t=\"" + svg_num(kT) + "\" data-ph=\"" + svg_num(ph) +
         "\" data-p=\"" + pj + "\">";
    // grid + y labels
    for (int p = 0; p <= 100; p += 25) {
        const std::string yy = svg_num(y(p));
        o += "<line class=\"grid\" x1=\"" + svg_num(kL) + "\" x2=\"" + svg_num(kW - kR) +
             "\" y1=\"" + yy + "\" y2=\"" + yy + "\"/>";
        o += "<text class=\"axis\" x=\"" + svg_num(kL - 6) + "\" y=\"" + svg_num(y(p) + 4) +
             "\" text-anchor=\"end\">%" + std::to_string(p) + "</text>";
    }
    // x ticks on local boundaries
    std::int64_t tick = local_midnight(from, in.utc_offset_sec);
    while (tick < from) {
        tick += r.tick;
    }
    for (; tick <= to; tick += r.tick) {
        const std::string label = r.tick >= kDay ? fmt_wday(tick, in.utc_offset_sec)
                                                 : fmt_hm(tick, in.utc_offset_sec);
        o += "<text class=\"axis\" x=\"" + svg_num(x(tick)) + "\" y=\"" + svg_num(kH - 8) +
             "\" text-anchor=\"middle\">" + label + "</text>";
    }

    const std::array<Series, 2> series = {Series{"Oturum", "--s1", true},
                                          Series{"Hafta", "--s2", false}};
    for (const Series& se : series) {
        std::string d;
        std::int64_t prev_ts = 0;
        bool open = false;
        const usage::Sample* last = nullptr;
        for (const auto* s : pts) {
            const auto& v = se.session ? s->session_pct : s->week_pct;
            if (!v) {
                open = false;
                continue;
            }
            const bool gap = open && s->ts - prev_ts > kGapSec;
            d += (!open || gap) ? "M" : "L";
            d += svg_num(x(s->ts)) + " " + svg_num(y(*v)) + " ";
            open = true;
            prev_ts = s->ts;
            last = s;
        }
        if (last == nullptr) {
            continue;
        }
        o += "<path class=\"line\" style=\"stroke:var(" + std::string(se.css_var) + ")\" d=\"" +
             d + "\"><title>" + se.name + "</title></path>";
        // direct label at the line end
        const double lv = se.session ? *last->session_pct : *last->week_pct;
        o += "<circle class=\"end\" style=\"fill:var(" + std::string(se.css_var) + ")\" cx=\"" +
             svg_num(x(last->ts)) + "\" cy=\"" + svg_num(y(lv)) + "\" r=\"4\"/>";
        o += "<text class=\"dl\" x=\"" + svg_num(x(last->ts) + 8) + "\" y=\"" +
             svg_num(y(lv) + (se.session ? -4 : 12)) + "\">" + se.name + " %" + fixed(lv, 0) +
             "</text>";
    }
    // crosshair + markers, moved by the inline script; parked off-canvas until hovered
    o += "<line class=\"xh\" x1=\"-10\" x2=\"-10\" y1=\"" + svg_num(kT) + "\" y2=\"" +
         svg_num(kT + ph) + "\"/><circle class=\"xm s\" cx=\"-10\" cy=\"0\" r=\"4\"/>"
         "<circle class=\"xm w\" cx=\"-10\" cy=\"0\" r=\"4\"/>";
    o += "</svg><div class=\"tip\" hidden></div><div class=\"legend\"><span><i style=\"background:var(--s1)\"></i>Oturum (5 saat)"
         "</span><span><i style=\"background:var(--s2)\"></i>Hafta (7 g\xC3\xBCn)</span></div>"
         "</figure>";
}

// ---- token breakdown --------------------------------------------------------

void breakdown_bars(std::string& o, const std::vector<const breakdown::Result*>& sessions) {
    o += "<figure class=\"chart\"><figcaption>Token da\xC4\x9F\xC4\xB1l\xC4\xB1m\xC4\xB1 "
         "(efektif)</figcaption>";
    const breakdown::Result total = merge(sessions);
    if (total.total_effective <= 0.0) {
        o += "<p class=\"empty\">Bu aral\xC4\xB1kta oturum d\xC3\xB6k\xC3\xBCm\xC3\xBC ya da "
             "\xC3\xB6zeti yok.</p></figure>";
        return;
    }
    o += "<p class=\"sub\">" + std::to_string(sessions.size()) + " oturum \xC2\xB7 " +
         std::to_string(total.request_count) + " istek \xC2\xB7 toplam " +
         format_tokens(total.total_effective) + "</p><div class=\"bars\">";
    const double top = total.groups.empty() ? 1.0 : total.groups.front().effective;
    for (const auto& g : total.groups) {
        const std::string name = escape_html(g.name);
        const std::string pct = fixed(100.0 * g.share, 1);
        o += "<div class=\"row\" title=\"" + name + ": " + format_tokens(g.effective) + " (%" +
             pct + ")\"><span class=\"lbl\">" + name +
             "</span><span class=\"track\"><span class=\"fill\" style=\"width:" +
             svg_num(std::max(0.5, 100.0 * g.effective / top)) + "%\"></span></span><span "
             "class=\"val\">%" + pct + " <em>" + format_tokens(g.effective) + "</em></span></div>";
    }
    o += "</div></figure>";
}

void session_table(std::string& o, const std::vector<const breakdown::Result*>& sessions,
                   std::int32_t off) {
    if (sessions.empty()) {
        return;
    }
    o += "<details><summary>Oturumlar (" + std::to_string(sessions.size()) +
         ")</summary><table><thead><tr><th>Biti\xC5\x9F</th><th>Proje</th><th>\xC4\xB0stek</th>"
         "<th>Efektif</th><th>En b\xC3\xBCy\xC3\xBCk grup</th></tr></thead><tbody>";
    for (const auto* s : sessions) {
        const std::string end = s->ended_at ? fmt_dt(*s->ended_at, off) : "\xE2\x80\x94";
        const std::string topg =
            s->groups.empty() ? "\xE2\x80\x94"
                              : escape_html(s->groups.front().name) + " %" +
                                    fixed(100.0 * s->groups.front().share, 0);
        o += "<tr><td>" + end + "</td><td>" +
             escape_html(s->project.empty() ? std::string("\xE2\x80\x94") : s->project) +
             "</td><td class=\"num\">" + std::to_string(s->request_count) +
             "</td><td class=\"num\">" + format_tokens(s->total_effective) + "</td><td>" + topg +
             "</td></tr>";
    }
    o += "</tbody></table></details>";
}

// ---- headline cards ---------------------------------------------------------

void window_card(std::string& o, const Input& in, const char* title, usage::Window w,
                 std::int64_t window_sec) {
    const usage::Sample* last = nullptr;
    for (auto it = in.samples.rbegin(); it != in.samples.rend(); ++it) {
        const auto& v = w == usage::Window::Session ? it->session_pct : it->week_pct;
        if (v) {
            last = &*it;
            break;
        }
    }
    o += "<div class=\"card\"><h3>" + std::string(title) + "</h3>";
    if (last == nullptr) {
        o += "<p class=\"big\">\xE2\x80\x94</p><p class=\"muted\">Veri yok</p></div>";
        return;
    }
    const double pct = w == usage::Window::Session ? *last->session_pct : *last->week_pct;
    const auto reset = w == usage::Window::Session ? last->session_reset : last->week_reset;
    o += "<p class=\"big\">%" + fixed(pct, 0) + "</p>";
    if (reset) {
        if (const auto p = usage::pace(pct, *reset, window_sec, in.now)) {
            const double d = p->delta_pct;
            const std::string side = d >= 0 ? "\xC3\xB6nde" : "geride";
            o += "<p>Tempo: <b>" + fixed(d < 0 ? -d : d, 0) + " puan " + side +
                 "</b> <span class=\"muted\">(beklenen %" + fixed(p->expected_pct, 0) +
                 ")</span></p>";
        }
        o += "<p>Reset: " + fmt_dt(*reset, in.utc_offset_sec) + " <span class=\"muted\">(" +
             time::format_remaining(*reset - in.now) + ")</span></p>";
    }
    const auto full = usage::projected_full(in.samples, w);
    o += full ? "<p>Tahmin: <b>" + fmt_dt(*full, in.utc_offset_sec) + "</b>'te %100</p>"
              : std::string("<p class=\"muted\">Tahmin: reset \xC3\xB6ncesi dolmuyor</p>");
    o += "<p class=\"muted\">Son \xC3\xB6rnek " + fmt_dt(last->ts, in.utc_offset_sec) + "</p></div>";
}

void rate_card(std::string& o, const Input& in) {
    o += "<div class=\"card\"><h3>Oturum \xE2\x86\x92 hafta kuru</h3>";
    const auto rate = usage::session_to_week_rate(in.samples);
    if (!rate) {
        o += "<p class=\"big\">\xE2\x80\x94</p><p class=\"muted\">Yeterli \xC3\xB6rnek yok "
             "(en az 1 oturum puan\xC4\xB1 art\xC4\xB1\xC5\x9F\xC4\xB1)</p></div>";
        return;
    }
    o += "<p class=\"big\">" + fixed(*rate, 2) + "</p><p>1 oturum puan\xC4\xB1 \xE2\x89\x88 " +
         fixed(*rate, 2) + " hafta puan\xC4\xB1</p>";
    if (*rate > 0.0) {
        o += "<p class=\"muted\">Tam bir oturum (%100) \xE2\x89\x88 haftan\xC4\xB1n %" +
             fixed(100.0 * *rate, 0) + "'i</p>";
    }
    o += "</div>";
}

// Crosshair for every limit chart. Only numbers from data-p are written into the tooltip.
constexpr const char* kHoverJs = R"JS(
document.querySelectorAll('svg.lc').forEach(function(svg){
var d=svg.dataset,P=JSON.parse(d.p),f=+d.from,t=+d.to,off=+d.off,L=+d.l,W=+d.pw,T=+d.t,H=+d.ph;
var fig=svg.parentNode,tip=fig.querySelector('.tip'),xh=svg.querySelector('.xh'),ms=svg.querySelector('.xm.s'),mw=svg.querySelector('.xm.w');
function X(s){return L+W*(s-f)/(t-f)}
function Y(p){return T+H*(1-Math.max(0,Math.min(100,p))/100)}
function z(n){return(n<10?'0':'')+n}
function fmt(s){var x=new Date((s+off)*1000);return z(x.getUTCDate())+'.'+z(x.getUTCMonth()+1)+' '+z(x.getUTCHours())+':'+z(x.getUTCMinutes())}
function pc(v){return v==null?'\u2014':'%'+v.toFixed(1).replace('.',',')}
function park(){tip.hidden=true;[xh].forEach(function(l){l.setAttribute('x1',-10);l.setAttribute('x2',-10)});ms.setAttribute('cx',-10);mw.setAttribute('cx',-10)}
svg.addEventListener('mousemove',function(e){
var r=svg.getBoundingClientRect(),vx=(e.clientX-r.left)/r.width*720;
if(vx<L||vx>L+W||!P.length){park();return}
var s=f+(vx-L)/W*(t-f),lo=0,hi=P.length-1;
while(hi-lo>1){var m=(lo+hi)>>1;if(P[m][0]<s)lo=m;else hi=m}
var p=Math.abs(P[lo][0]-s)<=Math.abs(P[hi][0]-s)?P[lo]:P[hi],x=X(p[0]);
xh.setAttribute('x1',x);xh.setAttribute('x2',x);
[[ms,p[1]],[mw,p[2]]].forEach(function(a){if(a[1]==null)a[0].setAttribute('cx',-10);else{a[0].setAttribute('cx',x);a[0].setAttribute('cy',Y(a[1]))}});
tip.innerHTML='<b>'+fmt(p[0])+'</b><br><i class="k s"></i>Oturum '+pc(p[1])+'<br><i class="k w"></i>Hafta '+pc(p[2]);
tip.hidden=false;
var fr=fig.getBoundingClientRect(),px=r.left-fr.left+x/720*r.width,left=px+12;
if(left+tip.offsetWidth>fr.width-8)left=px-12-tip.offsetWidth;
tip.style.left=left+'px';tip.style.top=(r.top-fr.top+8)+'px'});
svg.addEventListener('mouseleave',park)});
)JS";

constexpr const char* kCss = R"CSS(
:root{color-scheme:light;--bg:#f6f6f4;--surface:#fcfcfb;--text:#0b0b0b;--text2:#52514e;--muted:#7a7974;--grid:#e4e3df;--border:#dddcd7;--s1:#2a78d6;--s2:#eb6834;--track:#ecebe7}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){color-scheme:dark;--bg:#121211;--surface:#1a1a19;--text:#f0efec;--text2:#c3c2b7;--muted:#8f8e86;--grid:#2c2c2a;--border:#323230;--s1:#3987e5;--s2:#d95926;--track:#262624}}
:root[data-theme="dark"]{color-scheme:dark;--bg:#121211;--surface:#1a1a19;--text:#f0efec;--text2:#c3c2b7;--muted:#8f8e86;--grid:#2c2c2a;--border:#323230;--s1:#3987e5;--s2:#d95926;--track:#262624}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 "Segoe UI",system-ui,sans-serif}
main{max-width:980px;margin:0 auto;padding:20px 16px 40px}
header{display:flex;flex-wrap:wrap;align-items:baseline;justify-content:space-between;gap:8px}
h1{font-size:20px;margin:0}h3{margin:0 0 6px;font-size:13px;color:var(--text2);font-weight:600}
.muted,.sub{color:var(--muted)}.sub{margin:0 0 10px}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin:16px 0}
.card,.chart,details{background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:14px 16px}
.card p{margin:2px 0}.big{font-size:28px;font-weight:600;font-variant-numeric:tabular-nums}
.tabs{display:flex;gap:6px;margin:8px 0 12px}
.tabs label{padding:6px 12px;border:1px solid var(--border);border-radius:999px;cursor:pointer;color:var(--text2);background:var(--surface)}
input.tab{position:absolute;opacity:0;pointer-events:none}
.view{display:none}.view>*+*{margin-top:12px}
#v-sess:checked~main .view-sess,#v-day:checked~main .view-day,#v-week:checked~main .view-week{display:block}
#v-sess:checked~main label[for=v-sess],#v-day:checked~main label[for=v-day],#v-week:checked~main label[for=v-week]{background:var(--text);color:var(--surface);border-color:var(--text)}
input.tab:focus-visible~main .tabs{outline:2px solid var(--s1);outline-offset:4px;border-radius:999px}
figure{margin:0}figcaption{font-weight:600;margin-bottom:8px}
svg{width:100%;height:auto;display:block}
.grid{stroke:var(--grid);stroke-width:1}.axis{fill:var(--muted);font-size:11px}
.line{fill:none;stroke-width:2;stroke-linejoin:round;stroke-linecap:round}
.end{stroke:var(--surface);stroke-width:2}.dl{fill:var(--text2);font-size:11px}
.chart{position:relative}
.xh{stroke:var(--text2);stroke-width:1;stroke-dasharray:3 3}
.xm{stroke:var(--surface);stroke-width:2}.xm.s{fill:var(--s1)}.xm.w{fill:var(--s2)}
.tip{position:absolute;z-index:2;pointer-events:none;background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:6px 10px;font-size:12px;line-height:1.5;box-shadow:0 2px 8px rgba(0,0,0,.18);white-space:nowrap;font-variant-numeric:tabular-nums}
.tip .k{display:inline-block;width:8px;height:8px;border-radius:2px;margin-right:6px}.tip .k.s{background:var(--s1)}.tip .k.w{background:var(--s2)}
.legend{display:flex;gap:16px;color:var(--text2);font-size:12px;margin-top:6px}
.legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px;vertical-align:-1px}
.bars{display:grid;gap:6px}
.row{display:grid;grid-template-columns:minmax(110px,180px) 1fr minmax(110px,auto);gap:10px;align-items:center}
.lbl{color:var(--text2);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.track{height:12px;background:var(--track);border-radius:4px}.fill{display:block;height:100%;background:var(--s1);border-radius:0 4px 4px 0}
.row:hover .fill{filter:brightness(1.12)}
.val{font-variant-numeric:tabular-nums;text-align:right}.val em{color:var(--muted);font-style:normal;margin-left:4px}
.empty{color:var(--muted);margin:8px 0}
summary{cursor:pointer;font-weight:600}
table{width:100%;border-collapse:collapse;margin-top:10px;font-size:13px}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--grid)}th{color:var(--text2);font-weight:600}
td.num{text-align:right;font-variant-numeric:tabular-nums}
footer{margin-top:20px;color:var(--muted);font-size:12px}
@media (max-width:560px){.row{grid-template-columns:1fr auto}.row .track{grid-column:1/-1;order:3}}
)CSS";

}  // namespace

std::string escape_html(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string format_tokens(double v) {
    if (v < 1000.0) {
        return fixed(v, 0);
    }
    if (v < 1e6) {
        return fixed(v / 1e3, v < 1e4 ? 2 : 1) + " K";
    }
    return fixed(v / 1e6, v < 1e7 ? 2 : 1) + " M";
}

breakdown::Result merge(const std::vector<const breakdown::Result*>& parts) {
    breakdown::Result out;
    std::map<std::string, double> by;
    for (const auto* p : parts) {
        out.request_count += p->request_count;
        out.total_effective += p->total_effective;
        for (const auto& g : p->groups) {
            by[g.name] += g.effective;
        }
    }
    for (auto& [name, eff] : by) {
        breakdown::GroupCost g;
        g.name = name;
        g.effective = eff;
        g.share = out.total_effective > 0.0 ? eff / out.total_effective : 0.0;
        out.groups.push_back(std::move(g));
    }
    std::stable_sort(out.groups.begin(), out.groups.end(),
                     [](const auto& a, const auto& b) { return a.effective > b.effective; });
    return out;
}

std::string render_html(const Input& in) {
    const std::int32_t off = in.utc_offset_sec;
    const std::array<Range, 3> ranges = {
        Range{View::LastSession, "sess", "Son oturum", in.now - usage::kSessionWindowSec, 3600},
        Range{View::Today, "day", "Bug\xC3\xBCn", local_midnight(in.now, off), 3 * 3600},
        Range{View::Week, "week", "Bu hafta", in.now - 7 * kDay, kDay},
    };

    std::string o;
    o.reserve(64 * 1024);
    o += "<!doctype html><html lang=\"tr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
         "content=\"width=device-width,initial-scale=1\"><title>Claude kullan\xC4\xB1m "
         "raporu</title><style>";
    o += kCss;
    o += "</style></head><body>";
    for (const Range& r : ranges) {
        o += "<input class=\"tab\" type=\"radio\" name=\"v\" id=\"v-" + std::string(r.id) + "\"" +
             (r.view == in.default_view ? " checked" : "") + ">";
    }
    o += "<main><header><h1>Claude kullan\xC4\xB1m raporu</h1><span class=\"muted\">" +
         fmt_wday(in.now, off) + " " + fmt_hm(in.now, off) + "</span></header>";

    o += "<section class=\"cards\">";
    window_card(o, in, "Oturum (5 saat)", usage::Window::Session, usage::kSessionWindowSec);
    window_card(o, in, "Hafta (7 g\xC3\xBCn)", usage::Window::Week, usage::kWeekWindowSec);
    rate_card(o, in);
    o += "</section><nav class=\"tabs\">";
    for (const Range& r : ranges) {
        o += "<label for=\"v-" + std::string(r.id) + "\">" + r.label + "</label>";
    }
    o += "</nav>";

    for (const Range& r : ranges) {
        const auto sessions = sessions_in(in, r);
        o += "<section class=\"view view-" + std::string(r.id) + "\">";
        line_chart(o, in, r);
        breakdown_bars(o, sessions);
        session_table(o, sessions, off);
        o += "</section>";
    }
    o += "<footer>Efektif = girdi + 0,1\xC3\x97\xC3\xB6nbellek okuma + 2\xC3\x97\xC3\xB6nbellek "
         "yazma + 5\xC3\x97\xC3\xA7\xC4\xB1kt\xC4\xB1. Yaln\xC4\xB1z say\xC4\xB1lar ve ara\xC3\xA7 "
         "adlar\xC4\xB1 okunur; mesaj metni saklanmaz.</footer></main><script>";
    o += kHoverJs;
    o += "</script></body></html>";
    return o;
}

}  // namespace cub::report
