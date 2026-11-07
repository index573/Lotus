# FL Studio plugin SDK headers

`fp_plugclass.h` and `fp_def.h` come from Image-Line's FL Studio SDK (header revision 99-21), which Image-Line distributes for developers making native FL Studio plugins. They are Image-Line's code, not Lotus'.

They have one change: the `__APPLE__` checks also accept `__linux__`, and `__stdcall` is defined away on Linux. That lets the wrapper build on Linux, where the mock-host test runs. FL Studio itself only runs on Windows and macOS.
