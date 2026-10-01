#pragma once
namespace tab5 {
void viewBegin();
void viewUpdate();
void viewOpenSettings();
void viewOpenAdvanced();
void viewCloseAdvanced();
void viewReturnDashboard();
bool viewDiagnostic(const char *command);
}  // namespace tab5
