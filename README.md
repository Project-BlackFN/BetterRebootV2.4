# BetterRebootGS

**Version: 2.4**

**Original Project by [Milxnor](https://github.com/Milxnor) - [Project Reboot 3.0](https://github.com/Milxnor/Project-Reboot-3.0)**

#

**This Gameserver is an extension of [BetterMomentum](https://github.com/Project-BlackFN/BetterMomentum) because of this is an Extension of [BetterMomentum](https://github.com/Project-BlackFN/BetterMomentum) u should only use it if u have BetterMomentum**

**Features:**

- Register via. API (PROGRESS: 100%)
- per Kill 75 V-Bucks (PROGRESS: 0%)
- Auto Restart (PROGRESS: 100%) - [External Starter](https://github.com/Project-BlackFN/ServerStarter) needed


**Installation:**

1. Download this Project via git clone or Code Button

2. Open it with Visual Studio 2022 (you must have installed the Desktop Development Pack C++)

3. Switch to Release (x64) if this is'nt Selected as Default

4. go to Reboot/Public and add the File "settings.h" paste this into it:

```
#pragma once
#include <string>

inline std::string BACKEND_URL = "https://my.backend:3551";
inline std::string SERVER_AUTH_KEY = "secretkey123";
inline std::string WEBHOOK_UPTIME_URL = "";
inline std::string PUBLIC_IP = "PUBLIC IP OF GS";
#pragma once
```

5. Build it.. (rightclick on BetterRebootV2.4 and choose Rebuild)

6. now under the Main Folder (where the BetterRebootGS.sln is) should be a x64 Folder in this Folder u go into Release and there u have ur dll to inject

6. Inject it to the Fortnite Process via. Process Hacker or Xenos64 or u just use our [ServerStarter](https://github.com/Project-BlackFN/ServerStarter)