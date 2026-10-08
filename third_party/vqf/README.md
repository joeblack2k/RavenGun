# VQF (Versatile Quaternion-based Filter), ongewijzigd gevendord

- Bron: https://github.com/dlaidig/vqf (Daniel Laidig en Thomas Seel; TU Berlin)
- Commit: `86ba56bdd3158b9b05f9f9fe5596866ba326438c` (9 juli 2026, "Bump cibuildwheels and Ubuntu version for GitHub actions and readthedocs"), tag `v2.1.2`.
- Licentie: MIT, zie `LICENSE` (copyright 2021 Daniel Laidig; de SPDX-regels staan bovenin de bronbestanden).
- Overgenomen: alleen de C++-hoofdimplementatie `vqf/cpp/vqf.cpp` en `vqf/cpp/vqf.hpp`, byte voor byte, zonder patch. `basicvqf.*` en `offline_vqf.*` zijn niet nodig en niet meegenomen.
- Publicatie: D. Laidig en T. Seel, "VQF: Highly Accurate IMU Orientation Estimation with Bias Estimation and Magnetic Disturbance Rejection", Information Fusion 2023, 91, 187-204 (https://arxiv.org/abs/2203.17024).
- Gebruik in RavenGun: alleen via `src/ravengun/core/vqf_estimator.{hpp,cpp}` (fusiemodi `vqf` en `vqf_mag`). Bijwerken: nieuwe `vqf.cpp`/`vqf.hpp` uit dezelfde map van de bron kopiëren, commit en tag hierboven aanpassen, replay-uitkomsten (`ravengun-analyze drift`) opnieuw vergelijken.
- De bron wordt met `-w` gecompileerd (eigen waarschuwingsniveau van RavenGun is -Wall -Wextra -Wpedantic).
