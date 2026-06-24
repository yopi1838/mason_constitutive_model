# MASON — Masonry Advanced Softening cONtact Model for 3DEC

**MASON** (Masonry Advanced Softening cONtact) is a user-defined contact constitutive model for masonry joints in [*3DEC* 9.0](https://www.itascacg.com/software/3dec) (Itasca Consulting Group), developed within the framework of multi-surface plasticity for the Distinct Element Method (DEM).

The model combines three yield surfaces — a tension cut-off, a Coulomb friction surface in shear, and an elliptical compression cap — and is designed for the cyclic and dynamic analysis of unreinforced masonry structures, from single joints to full buildings.

Developed as part of the PhD dissertation *"Computational Modelling Strategy for Masonry Analysis Using the Distinct Element Method: A Robust Joint Constitutive Model for Cracking–Shearing–Crushing Damage under Cyclic Loading"*, Delft University of Technology.

## Features

- **Multi-surface plasticity**: tension cut-off (F1), Coulomb friction with cohesion and dilatancy (F2), and an elliptical compression cap (F3) adopted from Lourenço & Rots, active simultaneously depending on the stress state.
- **Fracture-energy-based softening**: tensile and shear strengths soften exponentially after the peak, governed by the mode-I (`G_I`) and mode-II (`G_II`) fracture energies, ensuring mesh-objective energy dissipation. User-defined piecewise softening via tables (`table-dt`, `table-ds`) is supported as an alternative.
- **Compressive crushing**: parabolic hardening to peak followed by softening controlled by the compressive fracture energy (`G_c`), with a confinement-dependent elliptical cap.
- **Non-associated shear flow**: dilatancy decays exponentially with shear slip and confinement (`dilation-zero`, dilatancy gradient `delta`), decoupling shear strength from joint uplift.
- **Mode-dependent cyclic rules**: secant-type degraded unloading in tension, nonlinear unloading–reloading in compression, and stiffness/strength degradation driven by three damage scalars (`dt`, `ds`, `dc`) with tension–shear coupling (`d_ts`). The degraded unloading softens re-contact transitions and attenuates spurious high-frequency content in dynamic analyses.
- **Energy tracking**: tensile, shear, and compressive contact energies (`energy-tension`, `energy-shear`, `energy-compression`) for hysteretic energy-dissipation studies.
- **Robust explicit integration**: anti-chatter dead-band at the tension/compression transition, damage clamping, and numerically stable stress-return algorithms for shake-table and rocking simulations.
- Works with both **rigid and deformable block** configurations.

## Repository structure

| Path | Description |
|---|---|
| `jmodelmason.h` / `jmodelmason.cpp` | MASON contact model source (class `JModelMason`, plugin `jmodelmason`) |
| `ItascaConstutitiveModel1.vcxproj` / `.slnx` | Visual Studio project/solution files |
| `Release/`, `out/Release/` | Pre-built DLL binaries |
| `version.txt` / `version.rc` | Plugin version information |
| `LICENSE.txt` | MIT license |

## Requirements

- *3DEC* 9.0 (the plugin interface headers `jointmodel.h` and `state.h` ship with the 3DEC C++ plugin SDK)
- Windows x64
- Visual Studio (MSVC toolchain) to build from source; a pre-built DLL is provided in `Release/`

## Installation

1. Build the solution in *Release | x64* (or use the pre-built `jmodelmason.dll` from `Release/`).
2. Copy the DLL into the 3DEC plugins folder so it is loaded at startup (e.g. `...\Itasca\3dec900\exe64\plugins\jmodel\`), or load it explicitly from your data file following the *User-Defined Constitutive Models* section of the 3DEC documentation.
3. The model is then available under the name **`mason`** (debug build: `masond`).

## Usage

```
; assign the MASON model to contacts
block contact jmodel assign mason

; assign material properties (units: Pa, Pa/m, N/m, degrees)
block contact property ...
    stiffness-normal  [kn]      stiffness-initial [kn0]    stiffness-shear [ks] ...
    tension           [ft]      cohesion          [c0]     friction        [phi] ...
    dilation          [psi0]    dilation-zero     [sigma_psi0] ...
    G_I               [GfI]     G_II              [GfII] ...
    compression       [fc]      G_c               [Gc]      peak_ratio      [n] ...
    Cn [Cn]  Cnn [Cnn]  Css [Css] ...
    friction-residual [phi_r]   cohesion-residual [c_r] ...
    tension-residual  [ft_r]    comp-residual     [fc_r]
```

### Input properties

| Keyword | Symbol | Description |
|---|---|---|
| `stiffness-normal` | $k_n$ | Normal contact stiffness (degrades with tensile damage) |
| `stiffness-initial` | $k_{n,0}$ | Initial (elastic) normal stiffness, reference for unloading and compression |
| `stiffness-shear` | $k_s$ | Shear contact stiffness |
| `tension` | $f_t$ | Tensile strength (tension cut-off) |
| `cohesion` | $c_0$ | Cohesive shear strength |
| `friction` | $\varphi$ | Peak friction angle (degrees) |
| `dilation` | $\psi_0$ | Initial dilatancy angle (degrees) |
| `dilation-zero` | $\sigma_{\psi_0}$ | Confining stress at which dilatancy vanishes |
| `delta` | $\delta$ | Dilatancy decay gradient (default 2 when dilation is set) |
| `G_I` | $G_f^I$ | Mode-I fracture energy (tensile softening) |
| `G_II` | $G_f^{II}$ | Mode-II fracture energy (shear softening) |
| `compression` | $f_c$ | Compressive strength (cap) |
| `G_c` | $G_c$ | Compressive fracture energy (**required**) |
| `peak_ratio` | $n$ | Ratio of displacement at peak compressive strength to elastic limit ($n \geq 1$, default 1) |
| `Cn`, `Cnn`, `Css` | $C_n, C_{nn}, C_{ss}$ | Compression-cap ellipse parameters (defaults 0, 1, 1) |
| `friction-residual` | $\varphi_r$ | Residual friction angle |
| `cohesion-residual` | $c_r$ | Residual cohesion |
| `tension-residual` | $f_{t,r}$ | Residual tensile strength |
| `comp-residual` | $f_{c,r}$ | Residual compressive strength |
| `table-dt`, `table-ds` | — | Optional user tables for tensile/shear damage evolution (mutually exclusive with `G_I`/`G_II`) |

Read-only/history variables (e.g. `dt`, `ds`, `dc`, `d_ts`, `fc_current`, `fric_current`, `dilation_current`, `un_hist_*`, `reloadFlag`) are exposed for monitoring damage and cyclic state via FISH.

### Contact states

`slip-n`, `tension-n`, `cap-n` (currently active) and `slip-p`, `tension-p`, `cap-p` (active in the past), for plotting failure modes.

### Notes

- `G_c` must be specified; otherwise initialization throws an error.
- Provide either a fracture energy (`G_I`/`G_II`) or a damage table (`table-dt`/`table-ds`) per mode, not both.
- If `compression` is omitted, the cap is effectively disabled (set to a very large value).

## Validation

The model has been validated across scales with a single parameter-identification strategy: joint-level tension, shear, and compression tests; component-level benchmarks including in-plane piers, the SERA cross-vault shake-table benchmark, and full-scale calcium-silicate gable walls tested to collapse; and the building-scale TUD-BUILD-1 two-storey calcium-silicate house under quasi-static cyclic pushover and incremental dynamic analysis — reproducing hysteresis, pinching, damage localisation, and collapse mechanisms without recalibration.

## Citation

If you use MASON in your research, please cite:

> Oktiovan, Y.P. (2026). *Computational Modelling Strategy for Masonry Analysis Using the Distinct Element Method: A Robust Joint Constitutive Model for Cracking–Shearing–Crushing Damage under Cyclic Loading*. PhD Dissertation, Delft University of Technology. Available at: https://repository.tudelft.nl/

## License

Released under the [MIT License](LICENSE.txt).

## Acknowledgements

Developed at the Faculty of Civil Engineering and Geosciences, Delft University of Technology. *3DEC* is a registered product of Itasca Consulting Group, Inc. The plugin interface follows the Itasca C++ user-defined model SDK.# ItascaConstutitiveModel1
