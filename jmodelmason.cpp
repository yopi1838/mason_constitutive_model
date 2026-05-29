#include "jmodelmason.h"
#include "state.h"
#include "version.txt"
#include <algorithm>
#include <limits>


int __stdcall DllMain(void*, unsigned, void*)
{
    return 1;
}

extern "C" __declspec(dllexport) const char* getName()
{
#ifdef JMODELDEBUG
    return "jmodelmasonhealingd";
#else
    return "jmodelmasonhealing";
#endif
}

extern "C" __declspec(dllexport) unsigned getMajorVersion()
{
    return MAJOR_VERSION;
}

extern "C" __declspec(dllexport) unsigned getMinorVersion()
{
    return UPDATE_VERSION;
}

extern "C" __declspec(dllexport) void* createInstance()
{
    jmodels::JModelMasonHealing* m = new jmodels::JModelMasonHealing();
    return (void*)m;
}

namespace jmodels
{
    static const double dPi = 3.141592653589793238462643383279502884197169399;
    static const double dDegRad = dPi / 180.0;
    static inline double clamp01(double v) {
        return (v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
    }
    // Clamp damage variables in [0,1] and snap to 1 once they are "close enough"
    // to avoid asymptotic approach without ever numerically reaching 1.
    #include <cmath> // ensure std::isfinite is available
    inline double clampDamage(double v) {
        if (v <= 0.0) return 0.0;
        if (v >= 1.0 - 1e-8) return 1.0;
        return v;
    }

    inline double clampToBand(double v, double lo, double hi) {
        if (!std::isfinite(v)) return lo;
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    // Plasticity Indicators
    static const uint32 slip_now = 0x01;  /* state logic */
    static const uint32 tension_now = 0x02;
    static const uint32 slip_past = 0x04;
    static const uint32 tension_past = 0x08;
    static const uint32 comp_now = 0x10;
    static const uint32 comp_past = 0x20;

    JModelMasonHealing::JModelMasonHealing() :
        kn_(0),
        kn_initial_(0),
        ks_(0),
        cohesion_(0),
        compression_(0),
        friction_(0),
        dilation_(0),
        tension_(0),
        s_zero_dilation_(0),
        res_cohesion_(0),
        res_friction_(0),
        res_tension_(0),
        res_comp_(0),
        tan_friction_(0),
        tan_dilation_(0),
        tan_res_friction_(0),
        G_I(0),
        G_II(0),
        G_c(0),
        dt(0),
        ds(0),
        dc(0),
        d_ts(0),
        cc(0),
        tP_(0),
        sP_(0),
        Cnn(0),
        Css(0),
        Cn(0),
        fc_current(0),
        friction_current_(0),
        m_(0),
        n_(0),
        R_violates(0),
        R_yield(0),
        uel_(0),
        un_hist_comp(0),
        peak_normal(0),
        ds_hist(0),
        un_ro(0),
        fm_ro(0),
        un_hist_ten(0),
        dt_hist(0),
        dc_hist(0),
        pertFlag(0),
        plasFlag(0),
        reloadFlag(0),
        delta(0),
        dilation_current(0),
        un_dilatant(0),
        dil_hist(0),
        ddil(0),
        kn_for_maxwell_(0),
        nlimit(0)
        , heal_enabled_(false)
        , heal_h_max_(0.3491)
        , heal_alpha_(6.0345)
        , heal_tau_(12.16)
        , heal_Er_(1.0)
        , heal_time_scale_(0.0)
        , heal_damage_threshold_(0.01)
        , heal_stress_threshold_(0.0)
        , heal_w_max_(0.5)
        , heal_RT_(0.0)
        , heal_n_cycles_(0.0)
        , heal_resting_(false)
        , heal_eta_last_(0.0)
        , heal_w_at_heal_(0.0)
        , heal_dt_end_(0.0)
        , heal_ds_end_(0.0)
        , heal_dc_end_(0.0)
    {
    }

    JModelMasonHealing::~JModelMasonHealing()
    {
        // Clean up any allocated resources here
        if (energies_)
            delete energies_;
    }

    string JModelMasonHealing::getName() const
    {
#ifdef JMODELDEBUG
        return "masonhealingd";
#else
        return "masonhealing";
#endif
    }

    string JModelMasonHealing::getFullName() const
    {
#ifdef JMODELDEBUG
        return "Mason Healing Debug";
#else
        return "Mason Healing";
#endif
    }

    uint32 JModelMasonHealing::getMinorVersion() const
    {
        return UPDATE_VERSION;
    }

    string JModelMasonHealing::getProperties() const
    {
        return("stiffness-normal       ,stiffness-initial    ,stiffness-shear        ,cohesion   ,compression  ,friction   ,dilation   ,"
            "tension   ,dilation-zero    ,cohesion-residual  ,friction-residual  , comp-residual ,"
            "tension-residual    , G_I   , G_II  ,dt ,ds ,dc ,d_ts   ,cc ,"
            "table-dt    ,table-ds ,"
            "tensile-disp-plastic    ,shear-disp-plastic ,"
            "G_c, Cn, Cnn, Css, fc_current,  fric_current,   peak_ratio, ult_ratio,uel,un_hist_comp,peak_normal,ds_hist,"
            "un_reloading,fm_reloading,un_hist_ten, dt_hist,dc_hist,delta,dilation_current,un_dilatant,dil_hist,ddil,reloadFlag,"
            "k_for_maxwell,nlimit,heal_enabled,heal_h_max,heal_alpha,heal_tau,heal_Er,heal_time_scale,heal_damage_threshold,"
            "heal_stress_threshold,heal_w_max,heal_RT,heal_n_cycles,heal_resting,heal_eta_last,heal_w_at_heal,heal_dt_end,heal_ds_end,heal_dc_end");
    }

    string JModelMasonHealing::getStates() const
    {
        return "slip-n,tension-n,slip-p,tension-p,cap-n,cap-p";
    }

    base::Property JModelMasonHealing::getProperty(uint32 index) const
    {
        switch (index)
        {
        case 1:  return kn_;
        case 2:  return kn_initial_;
        case 3:  return ks_;
        case 4:  return cohesion_;
        case 5:  return compression_;
        case 6:  return friction_;
        case 7:  return dilation_;
        case 8:  return tension_;
        case 9:  return s_zero_dilation_;
        case 10:  return res_cohesion_;
        case 11:  return res_friction_;
        case 12: return res_comp_;
        case 13: return res_tension_;
        case 14: return G_I;
        case 15: return G_II;
        case 16: return dt;
        case 17: return ds;
        case 18: return dc;
        case 19: return d_ts;
        case 20: return cc;
        case 21: return dtTable_;
        case 22: return dsTable_;
        case 23: return tP_;
        case 24: return sP_;
        case 25: return G_c;
        case 26: return Cn;
        case 27: return Cnn;
        case 28: return Css;
        case 29: return fc_current;
        case 30: return friction_current_;
        case 31: return n_;
        case 32: return m_;
        case 33: return uel_;
        case 34: return un_hist_comp;
        case 35: return peak_normal;
        case 36: return ds_hist;
        case 37: return un_ro;
        case 38: return fm_ro;
        case 39: return un_hist_ten;
        case 40: return dt_hist;
        case 41: return dc_hist;
        case 42: return delta;
        case 43: return dilation_current;
        case 44: return un_dilatant;
        case 45: return dil_hist;
        case 46: return ddil;
        case 47: return reloadFlag;
        case 48: return kn_for_maxwell_;
        case 49: return nlimit;
        case 50: return heal_enabled_;
        case 51: return heal_h_max_;
        case 52: return heal_alpha_;
        case 53: return heal_tau_;
        case 54: return heal_Er_;
        case 55: return heal_time_scale_;
        case 56: return heal_damage_threshold_;
        case 57: return heal_stress_threshold_;
        case 58: return heal_w_max_;
        case 59: return heal_RT_;
        case 60: return heal_n_cycles_;
        case 61: return heal_resting_;
        case 62: return heal_eta_last_;
        case 63: return heal_w_at_heal_;
        case 64: return heal_dt_end_;
        case 65: return heal_ds_end_;
        case 66: return heal_dc_end_;
        }
        return 0.0;
    }

    void JModelMasonHealing::setProperty(uint32 index, const base::Property& prop, uint32)
    {
        JointModel::setProperty(index, prop);
        switch (index)
        {
        case 1: kn_ = prop.to<double>();  break;
        case 2: kn_initial_ = prop.to<double>(); break;
        case 3: ks_ = prop.to<double>();  break;
        case 4: cohesion_ = prop.to<double>();  break;
        case 5: compression_ = prop.to<double>(); break;
        case 6: friction_ = prop.to<double>();  break;
        case 7: dilation_ = prop.to<double>();  break;
        case 8: tension_ = prop.to<double>();  break;
        case 9: s_zero_dilation_ = prop.to<double>();  break;
        case 10: res_cohesion_ = prop.to<double>();  break;
        case 11: res_friction_ = prop.to<double>();  break;
        case 12: res_comp_ = prop.to<double>(); break;
        case 13: res_tension_ = prop.to<double>();  break;
        case 14: G_I = prop.to<double>(); break;
        case 15: G_II = prop.to<double>(); break;
        case 16: dt = prop.to<double>(); break;
        case 17: ds = prop.to<double>(); break;
        case 18: dc = prop.to<double>(); break;
        case 19: d_ts = prop.to<double>(); break;
        case 20: cc = prop.to<double>(); break;
        case 21: dtTable_ = prop.to<string>();  break;
        case 22: dsTable_ = prop.to<string>();  break;
        case 23: tP_ = prop.to<double>(); break;
        case 24: sP_ = prop.to<double>(); break;
        case 25: G_c = prop.to<double>(); break;
        case 26: Cn = prop.to<double>(); break;
        case 27: Cnn = prop.to<double>(); break;
        case 28: Css = prop.to<double>(); break;
        case 29: fc_current = prop.to<double>(); break;
        case 30: friction_current_ = prop.to<double>(); break;
        case 31: n_ = prop.to<double>(); break;
        case 32: m_ = prop.to<double>(); break;
        case 33: uel_ = prop.to<double>(); break;
        case 34: un_hist_comp = prop.to<double>(); break;
        case 35: peak_normal = prop.to<double>(); break;
        case 36: ds_hist = prop.to<double>(); break;
        case 37: un_ro = prop.to<double>(); break;
        case 38: fm_ro = prop.to<double>(); break;
        case 39: un_hist_ten = prop.to<double>(); break;
        case 40: dt_hist = prop.to<double>(); break;
        case 41: dc_hist = prop.to<double>(); break;
        case 42: delta = prop.to<double>(); break;
        case 43: dilation_current = prop.to<double>(); break;
        case 44: un_dilatant = prop.to<double>(); break;
        case 45: dil_hist = prop.to<double>(); break;
        case 46: ddil = prop.to<double>(); break;
        case 47: reloadFlag = prop.to<double>(); break;
        case 48: kn_for_maxwell_ = prop.to<double>(); break;
        case 49: nlimit = prop.to<double>(); break;
        case 50: heal_enabled_ = prop.to<bool>(); break;
        case 51: heal_h_max_ = prop.to<double>(); break;
        case 52: heal_alpha_ = prop.to<double>(); break;
        case 53: heal_tau_ = prop.to<double>(); break;
        case 54: heal_Er_ = prop.to<double>(); break;
        case 55: heal_time_scale_ = prop.to<double>(); break;
        case 56: heal_damage_threshold_ = prop.to<double>(); break;
        case 57: heal_stress_threshold_ = prop.to<double>(); break;
        case 58: heal_w_max_ = prop.to<double>(); break;
        case 59: heal_RT_ = prop.to<double>(); break;
        case 60: heal_n_cycles_ = prop.to<double>(); break;
        case 61: heal_resting_ = prop.to<bool>(); break;
        case 62: heal_eta_last_ = prop.to<double>(); break;
        case 63: heal_w_at_heal_ = prop.to<double>(); break;
        case 64: heal_dt_end_ = prop.to<double>(); break;
        case 65: heal_ds_end_ = prop.to<double>(); break;
        case 66: heal_dc_end_ = prop.to<double>(); break;
        }
    }

    static const uint32 Dqs = 0;
    static const uint32 Dqt = 1;
    static const uint32 Dqkn = 2;
    static const uint32 Dqc = 3;
    static const uint32 D_un_hist = 4;

    void JModelMasonHealing::copy(const JointModel* m)
    {
        JointModel::copy(m);
        const JModelMasonHealing* mm = dynamic_cast<const JModelMasonHealing*>(m);
        if (!mm) throw std::runtime_error("Internal error: constitutive model dynamic cast failed.");
        kn_ = mm->kn_;
        kn_initial_ = mm->kn_initial_;
        ks_ = mm->ks_;
        cohesion_ = mm->cohesion_;
        compression_ = mm->compression_;
        friction_ = mm->friction_;
        dilation_ = mm->dilation_;
        tension_ = mm->tension_;
        s_zero_dilation_ = mm->s_zero_dilation_;
        res_cohesion_ = mm->res_cohesion_;
        res_friction_ = mm->res_friction_;
        res_comp_ = mm->res_comp_;
        res_tension_ = mm->res_tension_;
        tan_friction_ = mm->tan_friction_;
        tan_dilation_ = mm->tan_dilation_;
        tan_res_friction_ = mm->tan_res_friction_;
        G_I = mm->G_I;
        G_II = mm->G_II;
        dt = mm->dt;
        ds = mm->ds;
        dc = mm->dc;
        d_ts = mm->d_ts;
        cc = mm->cc;
        dtTable_ = mm->dtTable_;
        dsTable_ = mm->dsTable_;
        tP_ = mm->tP_;
        sP_ = mm->sP_;
        G_c = mm->G_c;
        fc_current = mm->fc_current;
        friction_current_ = mm->friction_current_;
        n_ = mm->n_;
        m_ = mm->m_;
        uel_ = mm->uel_;
        un_hist_comp = mm->un_hist_comp;
        peak_normal = mm->peak_normal;
        ds_hist = mm->ds_hist;
        un_ro = mm->un_ro;
        fm_ro = mm->fm_ro;
        un_hist_ten = mm->un_hist_ten;
        dt_hist = mm->dt_hist;
        dc_hist = mm->dc_hist;
        delta = mm->delta;
        dilation_current = mm->dilation_current;
        un_dilatant = mm->un_dilatant;
        dil_hist = mm->dil_hist;
        ddil = mm->ddil;
        reloadFlag = mm->reloadFlag;
        kn_for_maxwell_ = mm->kn_for_maxwell_;
        nlimit = mm->nlimit;
        Cnn = mm->Cnn;
        Css = mm->Css;
        Cn = mm->Cn;
        heal_enabled_ = mm->heal_enabled_;
        heal_h_max_ = mm->heal_h_max_;
        heal_alpha_ = mm->heal_alpha_;
        heal_tau_ = mm->heal_tau_;
        heal_Er_ = mm->heal_Er_;
        heal_time_scale_ = mm->heal_time_scale_;
        heal_damage_threshold_ = mm->heal_damage_threshold_;
        heal_stress_threshold_ = mm->heal_stress_threshold_;
        heal_w_max_ = mm->heal_w_max_;
        heal_RT_ = mm->heal_RT_;
        heal_n_cycles_ = mm->heal_n_cycles_;
        heal_resting_ = mm->heal_resting_;
        heal_eta_last_ = mm->heal_eta_last_;
        heal_w_at_heal_ = mm->heal_w_at_heal_;
        heal_dt_end_ = mm->heal_dt_end_;
        heal_ds_end_ = mm->heal_ds_end_;
        heal_dc_end_ = mm->heal_dc_end_;
    }

    void JModelMasonHealing::initialize(uint32 dim, State* s)
    {
        JointModel::initialize(dim, s);


        tan_friction_ = tan(friction_ * dDegRad);
        tan_res_friction_ = tan(res_friction_ * dDegRad);
        tan_dilation_ = tan(dilation_ * dDegRad);
        dilation_current = dilation_;
        if (!s->state_) kn_for_maxwell_ = kn_initial_;

        // For zero-tension joints, kn_ should always equal kn_initial_
        // (no secant degradation — behaves like Mohr-Coulomb in tension)
        if (tension_ <= 0.0) kn_ = kn_initial_;

        // Initialize compressive cap
        R_yield = 0.0;
        R_violates = 0.0;

        // Reset table indices
        iTension_d_ = iShear_d_ = iHard_d_ = nullptr;

        if (dtTable_.length()) iTension_d_ = s->getTableIndexFromID(dtTable_);
        if (dsTable_.length()) iShear_d_ = s->getTableIndexFromID(dsTable_);

        //// --- SAFE INITIALIZATION OF HISTORY VARIABLES ---
        if (!G_c)
            throw std::runtime_error("Internal error: Please input compressive fracture energy.");
        if (!n_) n_ = 1.0;
        if (n_ < 1.0)
            throw std::runtime_error("Internal error: peak_ratio (n) must be bigger than 1.0");

        if (G_I && iTension_d_)
            throw std::runtime_error("Internal error: either G_I or dtTable_ can be defined, not both.");

        if (G_II && iShear_d_)
            throw std::runtime_error("Internal error: either G_II or dsTable_ can be defined, not both.");

        if (dilation_ && !delta) delta = 2;
        if (!dilation_) {
            delta = 0.0;
            un_dilatant = 0.0;
        }

        // Ensure compressive values are reasonable
        if (!compression_) compression_ = 1e20;
        if (!res_comp_)    res_comp_ = 0.0;
        if (!Cn)           Cn = 0.0;
        if (!Cnn)          Cnn = 1.0;
        if (!Css)          Css = 1.0;

        // --- self-healing parameter sanity ---
        if (heal_h_max_ < 0.0) heal_h_max_ = 0.0;
        if (heal_h_max_ > 1.0) heal_h_max_ = 1.0;
        if (heal_alpha_ < 0.0) heal_alpha_ = 0.0;
        if (heal_tau_ <= 0.0) heal_tau_ = 12.16;
        if (heal_Er_ < 0.0) heal_Er_ = 0.0;
        if (heal_w_max_ <= 0.0) heal_w_max_ = 0.5;
        if (heal_damage_threshold_ < 0.0) heal_damage_threshold_ = 0.0;
    }

    // numerically-stable quadratic (q-formula) + finite clamp
    double JModelMasonHealing::solveQuadratic(double a, double b, double c) {
        // Returns the larger real root (used for projection), clamped finite.
        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) return 0.0;
        if (std::abs(a) < 1e-18) {
            if (std::abs(b) < 1e-18) return 0.0;
            double x = -c / b;
            return std::isfinite(x) ? x : 0.0;
        }
        double disc = b * b - 4.0 * a * c;
        if (!std::isfinite(disc)) {
            const double scale = std::max({ std::abs(a), std::abs(b), std::abs(c), 1.0 });
            a /= scale; b /= scale; c /= scale;
            disc = b * b - 4.0 * a * c;
        }
        if (disc < 0.0) disc = 0.0;
        double root_disc = std::sqrt(disc);
        // q-formula
        double q = -0.5 * (b + std::copysign(root_disc, b));
        double x1 = q / a;
        double x2 = (std::abs(q) > 1e-18) ? (c / q) : 0.0;
        double xr = (x1 > x2) ? x1 : x2;
        return std::isfinite(xr) ? xr : 0.0;
    }

    double JModelMasonHealing::getEnergy(uint32 i) const
    {
        double ret(0.0);
        if (!energies_)
            return ret;
        switch (i) {
        case kwETension:  return energies_->etension_;
        case kwECompression:  return energies_->ecompression_;
        case kwEShear:    return energies_->eshear_;
        }
        assert(0);
        return ret;
    }

    bool JModelMasonHealing::getEnergyAccumulate(uint32 i) const {
        // Returns TRUE if the corresponding energy is accumulated or FALSE otherwise.
        switch (i) {
        case kwETension:  return true; //Accumulate normal energy based on mode-I fracture energy
        case kwECompression:  return true; //Accumulate normal energy based on mode-I fracture energy
        case kwEShear:    return true;
        }
        assert(0);
        return false;
    }

    void JModelMasonHealing::setEnergy(uint32 i, const double& d) {
        // Set an energy value. 
        if (!energies_) return;
        switch (i) {
        case kwETension:  energies_->etension_ = d; return;
        case kwECompression:  energies_->ecompression_ = d; return;
        case kwEShear:    energies_->eshear_ = d; return;
        }
        assert(0);
        return;
    }


    // ======================================================================
    // applyHealing(): SIMPLIFIED 3-parameter recovery-time law.
    //   eta(w,RT) = h_max * exp(-alpha*w) * (1 - exp(-RT/tau))
    //   width  -> amplitude (ceiling);  tau -> single time constant.
    // Damage reduced from the frozen episode baseline via the phase-field map
    //   d_upd = 1 - sqrt((1-d_end)^2 + eta*[1-(1-d_end)^2]*Er)   per scalar.
    // ======================================================================
    void JModelMasonHealing::applyHealing()
    {
        const double w = heal_w_at_heal_;
        const double tau = (heal_tau_ > 1e-9) ? heal_tau_ : 1e-9;

        double Tt = 1.0 - std::exp(-heal_RT_ / tau);   // first-order saturation
        if (Tt < 0.0) Tt = 0.0;
        if (Tt > 1.0) Tt = 1.0;

        double eta = heal_h_max_ * std::exp(-heal_alpha_ * w) * Tt;
        if (eta < 0.0) eta = 0.0;
        if (eta > 1.0) eta = 1.0;
        heal_eta_last_ = eta;

        const double Er = (heal_Er_ > 0.0) ? heal_Er_ : 0.0;
        auto healScalar = [&](double d_end, double eff) -> double {
            const double u2 = (1.0 - d_end) * (1.0 - d_end);
            double hs = u2 + eff * (1.0 - u2) * Er;
            if (hs < 0.0) hs = 0.0;
            double du = 1.0 - std::sqrt(hs);
            if (du < 0.0)   du = 0.0;
            if (du > d_end) du = d_end;   // healing cannot exceed prior damage
            return du;
            };

        // tensile and shear bond addressed by CaCO3 bridging; compression halved
        dt = healScalar(heal_dt_end_, eta);        dt_hist = dt;
        ds = healScalar(heal_ds_end_, eta);        ds_hist = ds;
        dc = healScalar(heal_dc_end_, 0.5 * eta);  dc_hist = dc;

        d_ts = clampDamage(dt + ds - dt * ds);
        if (!std::isfinite(d_ts)) d_ts = 0.0;

        cc = res_cohesion_ + (cohesion_ - res_cohesion_) * (1.0 - d_ts);
        fc_current = compression_ * (1.0 - dc);
    }

    void JModelMasonHealing::run(uint32 dim, State* s)
    {
        JointModel::run(dim, s);

        bool jumptoDC = false;
        /* --- state indicator:                                  */
        /*     store 'now' info. as 'past' and turn 'now' info off ---*/
        if (s->state_ & slip_now) s->state_ |= slip_past;
        s->state_ &= ~slip_now;
        if (s->state_ & tension_now) s->state_ |= tension_past;
        s->state_ &= ~tension_now;
        if (s->state_ & comp_now) s->state_ |= comp_past;
        s->state_ &= ~comp_now;
        uint32 IPlas = 0;

        // Ensure energy structure is allocated once tracking is requested
        activateEnergy();

        if (!s->area_) return;

        //double kna = kn_ * s->area_;
        double ksa = ks_ * s->area_;
        double kn_comp_ = kn_initial_;
        if (!nlimit) nlimit = 1e-6;
        if (!s->state_) {
            s->working_[Dqs] = 0.0;
            s->working_[Dqt] = 0.0;
            //s->working_[Dqkn] = 0.0;
            s->working_[Dqc] = 0.0;
            s->working_[5] = 0.0;
        }
        double ucel_ = n_ * compression_ / kn_comp_;

        // normal force
        double fn0 = s->normal_force_;
        double uel_limit = compression_ / kn_comp_ / 5.0;
        double fn_old = s->normal_force_;          // force at start of step
        DVect3 fs_old = s->shear_force_;          // force at start of step
        double fn_new = fn_old;                    // we will modify this local only

        double dn_ = -s->normal_disp_inc_;     // your sign convention
        double un_current = -s->normal_disp_;
        double un_new = un_current + dn_;

        double sn_ = fn_old / s->area_;
        double dsn_ = (fn_new - fn_old) / s->area_;  // trial elastic stress increment for branch selection

        // ===================================================================
        // SELF-HEALING TRIGGER  (condition-based, recovery-time accumulating)
        // While a joint is damaged, narrow enough to bridge, and (optionally)
        // unloaded, accrue resting time RT and reduce damage via the simplified
        // healing law evaluated from a baseline frozen at episode entry. The
        // damage-evolution laws below are guarded with (heal_enabled_ &&
        // heal_resting_) so they cannot overwrite the healed state mid-rest.
        // ===================================================================
        if (heal_enabled_) {
            const double w_now = std::abs(un_hist_ten);
            const double dmax = std::max(std::max(dt_hist, ds_hist), dc_hist);
            bool eligible = (dmax > heal_damage_threshold_) && (w_now < heal_w_max_);
            if (eligible && heal_stress_threshold_ > 0.0)
                eligible = (std::abs(sn_) < heal_stress_threshold_);
            if (eligible) {
                if (!heal_resting_) {                 // entering a rest episode
                    heal_dt_end_ = dt_hist;
                    heal_ds_end_ = ds_hist;
                    heal_dc_end_ = dc_hist;
                    heal_w_at_heal_ = w_now;
                    heal_resting_ = true;
                }
                if (heal_time_scale_ > 0.0) heal_RT_ += heal_time_scale_;
                applyHealing();
            }
            else if (heal_resting_) {               // reloaded -> episode ends
                heal_n_cycles_ += 1.0;
                heal_RT_ = 0.0;
                heal_resting_ = false;
            }
        }
        else if (heal_resting_) {
            heal_n_cycles_ += 1.0;
            heal_RT_ = 0.0;
            heal_resting_ = false;
        }

        constexpr double kEps = std::numeric_limits<double>::epsilon();

        //Calculate elastic limit
        double fel_limit = compression_ / 5.0;
        double fpeak = compression_;
        //double ftemp = 0.0;        

        // --- PRE-UPDATE STIFFNESS FOR TENSION (before force calculation) ---
        // Only degrade kn_ if there is actual tensile capacity to soften from.
        // With tension_ = 0 (pure friction joint), kn_ stays at kn_initial_
        // and the joint behaves like Mohr-Coulomb: zero force in tension, full kn on closure.
        if (tension_ > 0.0 && un_current < 0.0 && s->state_) {
            // Update tensile history
            if (dn_ < 0.0 && un_current <= un_hist_ten) {
                un_hist_ten = un_current;
                s->working_[D_un_hist] = un_hist_ten;
            }


        }

        // --- TENSION BRANCH --------------------------------------------------
        // Opening (dn_ < 0) = loading; Closing (dn_ > 0) = unloading (secant)        
        if (un_current < 0.0) {
            // --- TENSION BRANCH ---
            if (tension_ <= 0.0) {
                // Pure friction joint (like Mohr-Coulomb): zero tensile force while open.
                // On closure, apply kn_initial_ only for the compression portion.
                if (dn_ > 0.0 && un_new >= 0.0) {
                    // Closing past zero: compression portion only
                    double dn_comp = un_new;  // = dn_ - (-un_current), only the part past zero
                    fn_new = kn_initial_ * s->area_ * dn_comp;
                }
                else {
                    // Still open or opening further: zero force
                    fn_new = 0.0;
                }
            }
            else {
                // Softening tension joint: use degraded kn_

                // update tensile history as before
                if (dn_ < 0.0 && un_current <= un_hist_ten) {
                    un_hist_ten = un_current;
                    s->working_[D_un_hist] = un_hist_ten;
                }
                // Check if closing back into compression this step
                // Purely in tension
                const double kna_t = kn_ * s->area_;
                double dfn_t = kna_t * dn_;
                fn_new += dfn_t;
            }
        }
        else {//COMPRESSION BRANCH --------------------------------------------------
            // Update unloading history
            if (un_current >= un_hist_comp && reloadFlag == 0 && dn_ >= 0.0) {
                un_hist_comp = un_current;   // record current displacement for unloading
            }
            // ---------------- Monotonic loading in compression ----------------
            if ((sn_ + dsn_ >= peak_normal) && ((s->state_ & comp_past) == 0)) {
                double kna_el = kn_comp_ * s->area_;
                reloadFlag = 0;

                if (un_current <= uel_limit) {
                    // Purely elastic loading
                    double dfn = kna_el * dn_;
                    fn_new += dfn;
                    fc_current = fn_new / s->area_;
                    peak_normal = fc_current;
                }
                else if (!s->state_ || sn_ + dsn_ < compression_) {
                    // Onto nonlinear compression envelope
                    double x_new = (un_current - uel_limit) / ucel_;
                    plasFlag = 1;

                    // 2x - x^2 >= 0 guard
                    auto safe_sqrt_expr = [](double x)->double {
                        double val = 2.0 * x - x * x;
                        return (val >= 0.0) ? std::sqrt(val) : 0.0;
                        };
                    double fenv = fel_limit + (fpeak - fel_limit) * safe_sqrt_expr(x_new); // stress

                    double denom_un = un_current;
                    double k_trial = fenv / denom_un;

                    if (k_trial >= kn_comp_) {
                        // stay elastic this step
                        double dfn = kna_el * dn_;
                        fn_new += dfn;
                    }
                    else {
                        // go directly to envelope stress
                        fn_new = fenv * s->area_;
                    }

                    fc_current = fn_new / s->area_;
                    plasFlag = 1;
                    if (dn_ >= 0.0) peak_normal = fc_current;
                }
            }
            // ---------------- Unloading / reloading in compression -------------
            else {
                // Unloading in compression
                double mult = (dc > 0.0) ? 2.5 : 1.0;
                double r = (un_hist_comp / ucel_);
                double un_plastic_rat = 0.47 * mult * r * r + 0.5 * mult * r;
                double un_plastic = un_plastic_rat * ucel_;
                if (dn_ < 0.0 && (plasFlag == 1)) { // unloading from compression
                    // Smooth transition instead of sharp threshold at 98.5%
                    double prox_ratio = un_current / (un_hist_comp * 0.985);
                    double smooth_pert = std::min(1.0, std::max(0.0, (prox_ratio - 0.98) / 0.04)); // 0→1 over 98%-102%

                    if (sn_ + dsn_ > 0.0 && (smooth_pert < 0.5)) { // use midpoint instead of binary flag
                        // Nonlinear unloading (Xeta curve)
                        double k1 = 1.5 * kn_comp_;
                        double k2 = 0.15 * kn_comp_ / std::pow(1.0 + (un_hist_comp / ucel_), 2);

                        // Es
                        double denom_Es = (un_hist_comp - un_plastic);
                        if (std::abs(denom_Es) < kEps)
                            denom_Es = (denom_Es >= 0 ? kEps : -kEps);
                        double Es = peak_normal / denom_Es;

                        // Xeta
                        double denom_X = (un_plastic - un_hist_comp);
                        if (std::abs(denom_X) < kEps)
                            denom_X = (denom_X >= 0.0 ? kEps : -kEps);

                        double Xeta = (un_new - un_hist_comp) / denom_X;

                        // clamp Xeta to avoid extreme stiffness / non-objective spikes
                        if (Xeta < -5.0) Xeta = -5.0;
                        else if (Xeta > 5.0) Xeta = 5.0;


                        double B1 = k1 / Es;
                        double B3 = 2.0 - (k2 / Es) * (1.0 + B1);
                        double B2 = B1 - B3;

                        double denom_R = 1.0 + B2 * Xeta + B3 * Xeta * Xeta;
                        if (std::abs(denom_R) < kEps)
                            denom_R = (denom_R >= 0 ? kEps : -kEps);

                        double numer_R = (B1 * Xeta + Xeta * Xeta);
                        double fm = peak_normal + (1e-12 - peak_normal) * (numer_R / denom_R);

                        if (!std::isfinite(fm)) {
                            // fallback: linear elastic unloading
                            fm = peak_normal + kn_comp_ * (un_new - un_hist_comp);
                        }
                        if (sn_ < 0.0) {
                            fm += 0.0;
                        }
                        fn_new = fm * s->area_;
                        fc_current = fm;

                        // record for reloading
                        reloadFlag = 1;
                        fm_ro = fm;
                        un_ro = un_current;
                    }
                    else if (sn_ + dsn_ < 0.0) {
                        // unload all the way to zero
                        fm_ro = 0.0;
                        reloadFlag = 1;
                        fn_new += 0.0;
                        fc_current = 0.0;
                    }
                    else {
                        // purely elastic unloading from peak
                        fm_ro = 0.0;
                        reloadFlag = 0;
                        double dfn = kn_comp_ * s->area_ * dn_;
                        fn_new += dfn;
                        fc_current = fn_new / s->area_;
                    }
                }
                else {
                    // Reloading branch
                    if (un_current < un_ro && dn_ >= 0.0) {
                        // hold force; just flag reloading
                        reloadFlag = 1;
                        fc_current = fn_new / s->area_;
                    }
                    else if (reloadFlag == 1 && dn_ >= 0.0) {
                        double denom = un_hist_comp;
                        if (un_ro != 0.0)
                            denom = un_hist_comp - un_ro;

                        double k_re = kn_initial_;
                        double fm_re = 0.0;
                        double beta = 1.0;

                        double un_rec = (un_hist_comp - un_ro) / ucel_;
                        double un_rec_nz = std::max(0.0, un_rec);
                        if (un_hist_comp < ucel_) {
                            beta = 1.0 / (1.0 + 0.20 * std::sqrt(un_rec_nz));
                        }
                        else {
                            beta = 1.0 / (1.0 + 0.35 * std::pow(un_rec_nz, 0.2));
                        }

                        if (std::abs(denom) < 1e-12) {
                            k_re = kn_comp_;
                            fm_re = fm_ro;
                        }
                        else {
                            k_re = (beta * peak_normal - fm_ro) / denom;
                            fm_re = fm_ro + k_re * (un_current - un_ro);
                        }

                        if (dc > 0.0) {
                            // damaged compression cap
                            double fc_env = compression_ * (1.0 - dc);
                            if (fm_re < fc_env) {
                                fn_new = fm_re * s->area_;
                                fc_current = fm_re;
                            }
                            else {
                                reloadFlag = 0;
                                jumptoDC = true;
                            }
                        }
                        else {
                            // undamaged envelope
                            double x_env = (un_current - uel_limit) / ucel_;
                            double ftemp_env = fel_limit
                                + (fpeak - fel_limit)
                                * std::sqrt(std::max(0.0, 2.0 * x_env - x_env * x_env));
                            double fc_env = ftemp_env;

                            if (fm_re < fc_env) {
                                fn_new = fm_re * s->area_;
                                fc_current = fm_re;
                            }
                            else {
                                fn_new = fc_env * s->area_;
                                fc_current = fc_env;
                                reloadFlag = 0;
                            }
                        }

                        fc_current = fn_new / s->area_;
                    }
                    else {
                        // Purely elastic unloading
                        double dfn = kn_comp_ * s->area_ * dn_;
                        fn_new += dfn;
                        fc_current = fn_new / s->area_;
                        reloadFlag = 0;
                    } //unloading  
                }
            }
        }//compression

        // Central normal-force update (after tension/compression law)
        s->normal_force_inc_ = fn_new - fn_old;
        s->normal_force_ = fn_new;

        double ten;
        double comp = 0.0;
        const double eps = 1e-12;

        // Require sane compression parameters
        if (!std::isfinite(compression_) || compression_ <= eps) {
            dc = 0.0;
            dc_hist = 0.0;
            comp = 0.0;
        }
        else {
            // sanitize residual compression into [0, compression_]
            if (!std::isfinite(res_comp_) || res_comp_ < 0.0) res_comp_ = 0.0;
            if (res_comp_ > compression_) res_comp_ = compression_;

            // If no softening is possible (res == peak), force dc=0 and skip exponent form
            if (std::abs(compression_ - res_comp_) <= eps) {
                dc = 0.0;
            }

            // continue with your existing mid_comp/m_/dc logic afterwards
            double mid_comp = res_comp_ + (compression_ - res_comp_) / 2.0;
            double beta_ = ucel_ * res_comp_; //Coefficient for calculating intermediate ratio
            double kappa_ = ucel_ * compression_;
            double gamma_ = 2.0;
            m_ = (G_c - 0.5 * (pow(compression_, 2) / (9 * kn_comp_)) - 0.5 * (ucel_ - uel_limit) * 1.3 * compression_
                + 0.75 * kappa_ + 0.25 * beta_) / (0.25 * kappa_ * (2 + gamma_) - 0.25 * beta_ * (2 - 3 * gamma_));
            if (m_ < 1.5) m_ = 1.5;
            double ucul_ = m_ * ucel_;

            //Define the softening on compressive strength
            if (s->state_ || jumptoDC) {
                if (!(heal_enabled_ && heal_resting_)) {  // skip damage recompute while healing
                    if ((un_current >= ucel_) && (un_current < ucul_)) {
                        dc = (1 - (mid_comp / compression_)) * pow((un_current - ucel_) / (ucul_ - ucel_), 2);
                        if (dn_ > 0.0) peak_normal = std::min(peak_normal, compression_ * (1 - dc));
                    }
                    else if (un_current >= ucul_) {
                        double alpha = 2 * (mid_comp - compression_) / (ucul_ - ucel_);
                        dc = 1 - (res_comp_ / compression_) - ((mid_comp - res_comp_) / compression_) * exp(alpha * (un_current - ucul_) / (mid_comp - res_comp_));
                        if (dn_ > 0.0) peak_normal = std::min(peak_normal, compression_ * (1 - dc));
                    }
                    else {
                        dc = 0.0;
                    }
                    // Clamp compressive damage to avoid infinite approach to 1
                    dc = clampDamage(dc);
                    dc_hist = clampDamage(dc_hist);
                    if (dc >= dc_hist) dc_hist = dc;
                    else dc = dc_hist;
                } // end self-healing guard (compression damage)

                comp = compression_ * (1 - dc) * s->area_;
            }
            else {
                dc = 0.0;
                comp = compression_ * (1 - dc) * s->area_;
            }
        }

        fc_current = comp / s->area_;
        uel_ = (kn_initial_ > 0.0) ? tension_ / kn_initial_ : 0.0;
        //Define the softening tensile strength
        if (s->state_ && (tension_ > 0.0 || cohesion_ > 0.0))
        {
            bool sign = std::signbit(dn_);
            if (sign && tension_ > 0.0 && !(heal_enabled_ && heal_resting_)) {
                if (iTension_d_) {
                    tP_ = s->normal_disp_ / (tension_ / kn_initial_);
                    dt = s->getYFromX(iTension_d_, tP_); //if table_dt is provided.
                }
                else if (std::isfinite(G_I) && G_I > 0.0) {
                    tP_ = s->normal_disp_ - (tension_ / kn_initial_);
                    dt = 1.0 - exp(-tension_ / G_I * (s->normal_disp_ - (tension_ / kn_initial_))); //Exponential Softening
                }
            }// Update tensile stiffness BEFORE computing forces

            if (dt_hist < dt) dt_hist = dt;
            else dt = dt_hist;
            // Clamp tensile damage to avoid infinite approach to 1
            dt = clampDamage(dt);
            dt_hist = clampDamage(dt_hist);
            d_ts = clampDamage(dt + ds - dt * ds);
            if (!std::isfinite(d_ts)) d_ts = 0.0;
            // Note: Stiffness kn_ is now updated BEFORE force calculation (see above)
            // to avoid one-cycle lag and ensure consistency during cyclic loading
            // NOTE: kn_ (tensile unloading secant) is no longer updated here.
            // It is computed once per step at the end of run() as a single
            // source of truth, driven by mode-I damage dt only (see end of run()).

        }
        const double dts_eps = 1e-6; // practical threshold for "fully damaged"
        const double dts_eff = std::min(std::max(d_ts, 0.0), 1.0);

        double ten_strength = res_tension_ + (tension_ - res_tension_) * (1.0 - dts_eff);
        if (dts_eff >= 1.0 - dts_eps) ten_strength = res_tension_;   // exactly residual at full damage

        ten = -ten_strength * s->area_;

        // check tensile failure
        bool tenflag = false;
        double f1;
        f1 = s->normal_force_ - ten;
        // Change the criterion to f1 criterion for tensile instead
        if (f1 <= 0)
        {
            tenflag = tensionCorrection(s, &IPlas, ten, tenflag);
        }
        // Compressive cap "failure" flag: when dc is near fully damaged in compression
        const bool compflag = (dc >= 0.99);
        double dilation_c_step = 0.0;
        // Pre-compute dilatancy contribution for this step
        if (dilation_ && s->state_ && !compflag) {
            double dil_0 = dilation_;
            double tmax_dil = cohesion_ + tan((friction_ + dil_0) * dDegRad) * s->normal_force_ / s->area_;
            double usel_dil = (ks_ > 0.0) ? tmax_dil / ks_ : 0.0;
            const double usm_raw = s->shear_disp_.mag() - usel_dil;
            const double usm = std::max(0.0, usm_raw);
            const double zdd = std::max(s_zero_dilation_, 1e-12);
            const double delta_eff = std::max(0.0, delta);
            double tan_psi = tan_dilation_ * (1.0 - (usm / zdd)) * std::exp(-delta_eff * usm);
            if (!std::isfinite(tan_psi) || tan_psi < 0.0) tan_psi = 0.0;
            tan_psi = std::min(tan_psi, tan_dilation_);
            dilation_c_step = tan_psi;
        }
        // shear force
        if (!tenflag && !compflag)
        {
            s->shear_force_inc_ = s->shear_disp_inc_ * -ksa;
            s->shear_force_ += s->shear_force_inc_;

            //Because the normal force is already in negative anyway, we don't have to change the signs
            double dil_0 = 0.0;
            if (dilation_) dil_0 = dilation_;
            double fsmax = (cohesion_ * s->area_ + tan((friction_ + dil_0) * dDegRad) * s->normal_force_);
            double fsm = s->shear_force_.mag();
            double f2;
            double tmax = cohesion_ + tan((friction_ + dil_0) * dDegRad) * s->normal_force_ / s->area_;
            double usel = tmax / ks_;
            if (fsmax < 0.0) fsmax = 0.0;
            if (s->state_) {
                //Calculate max shear stress                            

                ////Exponential Softening
                // Only compute shear damage if there is cohesion to soften from
                if (cohesion_ > 0.0 && !(heal_enabled_ && heal_resting_)) {
                    if (iShear_d_) {
                        sP_ = s->shear_disp_.mag() / usel;
                        ds = s->getYFromX(iShear_d_, sP_);
                    }
                    else if (std::isfinite(G_II) && G_II > 0.0) {
                        sP_ = s->shear_disp_.mag() - usel;
                        ds = 1 - exp(-cohesion_ / G_II * (s->shear_disp_.mag() - usel));
                    }
                }
                if (ds >= ds_hist) ds_hist = ds;
                else ds = ds_hist;

                // Clamp shear damage to avoid infinite approach to 1
                ds = clampDamage(ds);
                ds_hist = clampDamage(ds_hist);
                d_ts = clampDamage(dt + ds - dt * ds);
                if (!std::isfinite(d_ts)) d_ts = 0.0;
                double resamueff = tan_res_friction_;
                if (!resamueff) resamueff = tan_friction_;
                cc = res_cohesion_ + (cohesion_ - res_cohesion_) * (1 - d_ts);

                // --- prerequisites (already in your scope) ---
                // ds, friction_, res_friction_, dil_0, dilation_, tan_dilation_, s_zero_dilation_, delta, usel
                // dDegRad defined
                // cc already computed (possibly via d_ts)
                // s->area_, s->normal_force_, s->shear_disp_.mag()

                const double ds_eff = std::min(std::max(d_ts, 0.0), 1.0);
                const double soft_ds = 1.0 - ds_eff;

                const double phi_peak_deg = friction_;
                const double phi_res_deg = (res_friction_ > 0.0 ? res_friction_ : friction_);

                double phi_eff_deg = phi_peak_deg;
                double psi0_eff_deg = 0.0;

                // Dilatancy considered for strength only if enabled AND dil_0 provided
                const bool use_dilatancy_strength = (dilation_ && (dil_0 > 0.0));

                if (use_dilatancy_strength) {

                    // --- dilatancy weakening modifies tc via psi0_eff_deg (slip-based rule) ---
                    const double usm_raw = s->shear_disp_.mag() - usel;
                    const double usm = std::max(0.0, usm_raw);
                    const double zdd = std::max(s_zero_dilation_, 1e-12);
                    const double delta_eff = std::max(0.0, delta);

                    double tan_psi_eff =
                        tan_dilation_ * (1.0 - (usm / zdd)) * std::exp(-delta_eff * usm);

                    if (!std::isfinite(tan_psi_eff)) tan_psi_eff = 0.0;
                    tan_psi_eff = std::min(std::max(tan_psi_eff, 0.0), tan_dilation_);

                    // keep phi fixed (no frictional softening when dilatancy governs the weakening)
                    phi_eff_deg = phi_peak_deg;
                    psi0_eff_deg = std::atan(tan_psi_eff) / dDegRad;

                }
                else {
                    // --- NO dilatancy: apply ds-based frictional softening (Option B) ---
                    psi0_eff_deg = 0.0;
                    phi_eff_deg = phi_res_deg + (phi_peak_deg - phi_res_deg) * soft_ds;
                }

                // Safety clamp before tan()
                const double ang_deg = std::min(std::max(phi_eff_deg + psi0_eff_deg, 0.0), 85.0);
                const double mu_eff = std::tan(ang_deg * dDegRad);

                double tc = cc * s->area_ + s->normal_force_ * mu_eff;
                if (!std::isfinite(tc) || tc < 0.0) tc = 0.0;

                fsmax = tc;
                f2 = fsm - tc;

            }
            else {
                f2 = fsm - fsmax;
                cc = cohesion_;
                friction_current_ = friction_ + dil_0;
            }// if (state)

            //Check if slip
            if (f2 >= 0.0)
            {
                // Slip ratio used by shearCorrection (projection)
                double rat = 0.0;
                if (fsm > 1e-30) rat = std::clamp(fsmax / fsm, 0.0, 1.0);

                // Plastic part of shear increment (approx.)
                const double slip_frac = std::clamp(1.0 - rat, 0.0, 1.0);
                const double dusm_pl = slip_frac * s->shear_disp_inc_.mag();

                // Apply shear projection
                shearCorrection(s, &IPlas, fsm, fsmax);

                // Apply dilatancy normal pumping ONLY from plastic slip, and only if dilation active
                if (dilation_ && dc == 0.0 && dilation_c_step > 0.0 && dusm_pl > 0.0)
                {
                    un_dilatant += dilation_c_step * dusm_pl;
                    const double dfn_dil = kn_ * s->area_ * dilation_c_step * dusm_pl;
                    s->normal_force_ += dfn_dil;
                }

                // Now cap-check using the updated normal force
                if (s->normal_disp_ < 0.0) {
                    const double f3 =
                        Cnn * s->normal_force_ * s->normal_force_
                        + Css * std::pow(s->shear_force_.mag(), 2)
                        + Cn * s->normal_force_
                        - std::pow(comp, 2);
                    if (f3 >= 0.0) compCorrection(s, &IPlas, comp);
                }
            }
            // if (f2)
            //Check compressive failure (compressive cap)
            if (s->normal_disp_ < 0.0) {
                //Check f3
                double f3;
                f3 = Cnn * pow(s->normal_force_, 2) + Css * pow(s->shear_force_.mag(), 2) + Cn * s->normal_force_ - pow(comp, 2);
                if (f3 >= 0.0) {
                    compCorrection(s, &IPlas, comp);
                    if (f2 >= 0.0) {
                        shearCorrection(s, &IPlas, fsm, fsmax);
                    }
                }
            }//s->normal_disp < 0.0
        } // if (!tenflag && !compflag)
        else {
            // In open tension or full compressive cap failure, there is no shear transfer
            s->shear_force_inc_ = DVect3(0.0, 0.0, 0.0);
            s->shear_force_ = DVect3(0.0, 0.0, 0.0);
        }

        // --- Energy accumulation (normal tension / compression + shear) ---
        if (energies_) {
            // New forces at end of the step
            const double fn_new_x = s->normal_force_;
            const DVect3 fs_new = s->shear_force_;

            // Displacement increments for this step
            // In your convention: normal_disp < 0 in compression.
            // We define du_n > 0 for compression by flipping the sign.
            const double du_n = -s->normal_disp_inc_;   // + = compression increment
            const DVect3 du_s = s->shear_disp_inc_;     // shear slip increment

            // Mean forces over the step
            const double fn_mean = 0.5 * (fn_old + fn_new_x);
            const DVect3 fs_mean(
                0.5 * (fs_old.x() + fs_new.x()),
                0.5 * (fs_old.y() + fs_new.y()),
                0.5 * (fs_old.z() + fs_new.z())
            );
            // Incremental normal work (positive = storing elastic energy,
            // negative = releasing it during unloading / damage).
            const double dWn = fn_mean * du_n;

            // Split normal energy into tension vs compression based on sign of mean force:
            //   fn_mean >= 0  -> compression branch
            //   fn_mean < 0   -> tension branch
            if (fn_mean >= 0.0) {
                energies_->ecompression_ += dWn;
            }
            else {
                energies_->etension_ += dWn;
            }

            // Incremental shear work: fs · du_s
            const double dWs =
                fs_mean.x() * du_s.x() +
                fs_mean.y() * du_s.y() +
                fs_mean.z() * du_s.z();

            energies_->eshear_ += dWs;
        }

        // FIX: enforce step-consistent force increments after all projections/couplings
        // (critical for large-strain runs where contact frames re-orient frequently)
        s->normal_force_inc_ = s->normal_force_ - fn_old;
        s->shear_force_inc_ = s->shear_force_ - fs_old;

        // FIX: recompute dnop_ using the final increment (opening within the step)
        s->dnop_ = s->normal_disp_inc_;
        if ((fn0 > 0.0) && (s->normal_force_ <= 0.0) && (s->normal_force_inc_ < 0.0))
        {
            s->dnop_ = -s->normal_disp_inc_ * fn0 / s->normal_force_inc_;
            if (s->dnop_ > s->normal_disp_inc_) s->dnop_ = s->normal_disp_inc_;
        }

        // ---------------------------------------------------------------
        // Centralized normal-stiffness update (single source of truth).
        // kn_ is the tensile unloading secant used by the NEXT step's
        // tension branch, the dilatancy coupling, and the Maxwell feed.
        // Driven by mode-I (tensile) damage dt ONLY, so shear damage no
        // longer bleeds into the normal-stiffness pathway. Evaluated every
        // step (the else covers the elastic / no-history case) -- the same
        // discipline TSCLinear uses for its single kn_ assignment.
        // ---------------------------------------------------------------
        {
            const double uel_t = (kn_initial_ > 0.0) ? (tension_ / kn_initial_) : 0.0;
            const bool tensile_history =
                (tension_ > 0.0) && ((un_new < -uel_t) || (un_hist_ten < -uel_t));
            if (tensile_history) {
                const double kn_min = std::max(1e-12, nlimit * kn_initial_);
                const double kn_max = kn_initial_;
                const double dt_eff = std::min(std::max(dt, 0.0), 1.0);   // dt, NOT d_ts
                const double denom = std::max(std::abs(un_hist_ten), 1e-9);
                double kn_cand = tension_ * (1.0 - dt_eff) / denom;
                kn_ = (std::isfinite(kn_cand) && kn_cand > 0.0)
                    ? std::clamp(kn_cand, kn_min, kn_max)
                    : kn_min;
            }
            else {
                kn_ = kn_initial_;   // no tensile failure history -> full stiffness
            }
        }

        // Operating normal stiffness reported via getMaxNormalStiffness()
        // (timestep + Maxwell damping): the per-state value, not the raw secant.
        // Compression responds at ~kn_initial_ so it is damped properly; open
        // tension carries the dt-driven secant.
        kn_for_maxwell_ = (un_current >= 0.0)
            ? kn_initial_ * (1.0 - dc)   // compression: ~elastic (dc~0 without crushing)
            : kn_;                         // tension: secant stiffness (dt-driven)
        // At end of run()
        if (std::isnan(s->normal_force_)) {
            throw std::runtime_error("NaN detected in JModelYopi::run normal side");
        }
        if (std::isnan(s->normal_force_) || std::isnan(s->shear_force_.x())
            || std::isnan(s->shear_force_.y()) || std::isnan(s->shear_force_.z())) {
            throw std::runtime_error("NaN detected in JModelYopi::run shear side");
        }
    }//run


    bool JModelMasonHealing::tensionCorrection(State* s, uint32* IPlasticity, double& ten, bool& tenflag)
    {
        if (IPlasticity) *IPlasticity = 1;
        s->state_ |= tension_now;

        const double ten_eps = 1e-9 * std::max(1.0, std::abs(tension_ * s->area_)); // scale-aware

        // Apply tensile cap (traction cannot exceed tensile capacity)
        s->normal_force_ = ten;

        // If capacity is essentially zero (or residual is intended to be zero), open the joint:
        if (std::abs(ten) <= ten_eps) {
            s->normal_force_ = 0.0;
            s->shear_force_ = DVect3(0.0, 0.0, 0.0);
            tenflag = true;
            return tenflag;
        }

        // Even with residual tension, do not keep a cohesive shear spring when in tension failure:
        // (Optional but strongly stabilizing in large-strain)
        s->shear_force_inc_ = DVect3(0, 0, 0);
        s->normal_force_inc_ = 0;
        tenflag = true;
        return tenflag;
    }


    void JModelMasonHealing::shearCorrection(State* s, uint32* IPlasticity, double& fsm, double& fsmax) {
        if (IPlasticity) *IPlasticity = 2;
        s->state_ |= slip_now;
        DVect3 fs_before = s->shear_force_;
        double rat = 0.0;
        if (fsm > 1e-30) rat = fsmax / fsm;
        s->shear_force_ *= rat;
        s->shear_force_inc_ = DVect3(0, 0, 0); // reset increment since we directly set the shear force
    }

    void JModelMasonHealing::compCorrection(State* s, uint32* IPlasticity, double& comp) {
        if (IPlasticity) *IPlasticity = 3;
        s->state_ |= comp_now;

        constexpr double EPS = 1e-12;

        const double x = s->normal_force_;
        const double y = s->shear_force_.mag();

        // If the point is already at the origin, do nothing
        if (std::abs(s->normal_force_) < EPS && s->shear_force_.mag() < EPS) {
            return;
        }

        // To improve numerical stability, solve the scaled equation:
        const double scale = std::max({ std::abs(x), std::abs(y), 1.0 });
        const double xs = x / scale;
        const double ys = y / scale;

        const double A = Cnn * xs * xs + Css * ys * ys;
        const double B = (Cn * xs) / scale;
        const double C = -(comp * comp) / (scale * scale);

        double lambda = solveQuadratic(A, B, C);


        if (!std::isfinite(lambda)) lambda = 0.0;
        if (lambda < 0.0) lambda = 0.0;
        if (lambda > 1.0) lambda = 1.0;

        // Full degradation branch
        if (dc >= 0.99) {
            // Residual compressive capacity (shear to zero at the cap)
            s->normal_force_ = res_comp_ * s->area_;
            s->shear_force_ = DVect3(0, 0, 0);
        }
        else {
            // Scale forces back to the cap along the ray from the origin
            s->normal_force_ = lambda * x;
            if (y > EPS) {
                const double ratc = lambda;
                s->shear_force_ *= ratc;    // preserve direction, scale magnitude
            }
            else {
                s->shear_force_ = DVect3(0, 0, 0);
            }
        }

        // Final safety check: catch Inf/overflow even when not NaN
        const auto badnum = [](double v) { return !std::isfinite(v) || std::abs(v) > 1e300; };
        if (badnum(s->normal_force_) || badnum(s->shear_force_.mag())) {
            throw std::runtime_error("JModelYopi::compCorrection produced non-finite/overflowed forces");
        }
    }
} // namespace models


// EOF