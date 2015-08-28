#pragma once

#ifndef _H_MAIN
#define _H_MAIN

#include "threads.h"

class textStream : public std::streambuf
{
public:
	textStream(wxTextCtrl *_text) { text = _text; }

protected:
	int_type overflow(int_type c)
	{
		buf.push_back(c);
		if (c == '\n')
		{
			text->AppendText(buf);
			buf.clear();
		}
		return c;
	}
private:
	wxTextCtrl *text;
	std::string buf;
};

class mainFrame : public wxFrame
{
public:
	mainFrame(const wxString& title);
	enum itemID{
		ID_FRAME,
		ID_LISTUSER, ID_BUTTONADD, ID_BUTTONDEL,
		ID_TEXTMSG, ID_TEXTINPUT, ID_BUTTONSEND, ID_BUTTONSENDFILE,
		ID_TEXTINFO,
		ID_SOCKETLISTENER, ID_SOCKETDATA,
		ID_SOCKETBEGIN_C1, ID_SOCKETBEGIN_C2, ID_SOCKETBEGIN_S1, ID_SOCKETBEGIN_S2
	};

	wxPanel *panel;

	wxListBox *listUser;
	wxButton *buttonAdd, *buttonDel;
	void listUser_SelectedIndexChanged(wxCommandEvent& event);
	void buttonAdd_Click(wxCommandEvent& event);
	void buttonDel_Click(wxCommandEvent& event);

	wxTextCtrl *textMsg, *textInput;
	wxButton *buttonSend, *buttonSendFile;
	void buttonSend_Click(wxCommandEvent& event);
	void buttonSendFile_Click(wxCommandEvent& event);

	void thread_Message(wxThreadEvent& event);

	void mainFrame_Close(wxCloseEvent& event);

	wxTextCtrl *textInfo;
	textStream *textStrm;
	std::streambuf *cout_orig, *cerr_orig;

	std::list<int> userIDs;

	wxDECLARE_EVENT_TABLE();
};

class wx_srv_interface :public server_interface
{
public:
	virtual void on_data(id_type id, const std::string &data);

	virtual void on_join(id_type id);
	virtual void on_leave(id_type id);

	virtual void on_unknown_key(id_type id, const std::string& key);

	void set_frame(mainFrame *_frm) { frm = _frm; }
private:
	std::unordered_set<iosrvThread*> threads;
	mainFrame *frm;
};

class MyApp : public wxApp
{
public:
	virtual bool OnInit();
	virtual int OnExit();
private:
	mainFrame *form;
};

#endif
