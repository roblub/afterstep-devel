// generated by Fast Light User Interface Designer (fluid) version 1.0103

#ifndef ascpui_h
#define ascpui_h
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Button.H>

class ASControlPanelUI {
public:
  ASControlPanelUI();
  Fl_Window *m_MainWindow;
  Fl_Menu_Bar *m_MainMenu;
  static Fl_Menu_Item menu_m_MainMenu[];
  Fl_Box *m_MainPreviewBox;
  Fl_Text_Display *m_MainPreviewText;
  Fl_Button *m_EditLookBtn;
  Fl_Button *m_EditFeelBtn;
  Fl_Button *m_DatabaseBtn;
  Fl_Button *m_BaseConfigBtn;
  Fl_Button *m_MenuBtn;
  Fl_Button *m_ModulesBtn;
  int show_main_window();
};
#endif
