#pragma once
namespace tab5 {
void viewBegin();
void viewUpdate();
void viewOpenSettings();
void viewOpenSelection();
void viewOpenAdvanced();
void viewCloseAdvanced();
void viewOpenGeneral();
void viewCloseGeneral();
void viewReturnDashboard();
bool viewDiagnostic(const char *command);
}  // namespace tab5
