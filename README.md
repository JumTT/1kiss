# unity-test

[1kiss NativeBridge](https://github.com/JumTT/1kiss/tree/main/src/nativebridge)(面向 Unity 的聚合原生库)的测试工程,Unity 版本 **2022.3**。

> 本分支是 orphan 分支,独立于 `main`,仅用于测试,**不会合并回 main**。
> 预编译二进制直接入库,clone 后无需额外下载。

## 更新二进制

NativeBridge 上游更新后,从 [nativebridge workflow](https://github.com/JumTT/1kiss/actions/workflows/nativebridge.yml) 下载最新 artifact,覆盖 `Assets/Plugins/NativeBridge/Plugins/` 下的对应文件并提交。

## 运行

1. 用 Unity 2022.3 打开本工程。
2. 打开 `Assets/Scenes/SampleScene` 运行。
3. 单元测试:Window → General → Test Runner(`NativeBridgeF.Tests`)。
