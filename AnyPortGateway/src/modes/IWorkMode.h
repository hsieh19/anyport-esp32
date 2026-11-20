#pragma once

/**
 * @brief 应用业务层：统一工作模式生命周期契约 (遵循 OCP 与 LSP)
 */
class IWorkMode {
public:
  virtual ~IWorkMode() = default;
  virtual void begin() = 0;
  virtual void loop() = 0;
};
