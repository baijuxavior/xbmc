#include "include.h"
#include "GUIDialog.h"
#include "GUIWindowManager.h"
#include "GUILabelControl.h"
#include "GUIAudioManager.h"


CGUIDialog::CGUIDialog(DWORD dwID, const CStdString &xmlFile)
    : CGUIWindow(dwID, xmlFile)
{
  m_dwParentWindowID = 0;
  m_pParentWindow = NULL;
  m_bModal = true;
  m_bRunning = false;
  m_dialogClosing = 0;
  m_renderOrder = 1;
}

CGUIDialog::~CGUIDialog(void)
{}

bool CGUIDialog::Load(const CStdString& strFileName, bool bContainsPath)
{
  if (!CGUIWindow::Load(strFileName, bContainsPath))
  {
    return false;
  }

  // Clip labels to extents
  if (m_vecControls.size())
  {
    CGUIControl* pBase = m_vecControls[0];

    for (ivecControls p = m_vecControls.begin() + 1; p != m_vecControls.end(); ++p)
    {
      if ((*p)->GetControlType() == CGUIControl::GUICONTROL_LABEL)
      {
        CGUILabelControl* pLabel = (CGUILabelControl*)(*p);

        if (!pLabel->GetWidth())
        {
          int spacing = (pLabel->GetXPosition() - pBase->GetXPosition()) * 2;
          pLabel->SetWidth(pBase->GetWidth() - spacing);
          pLabel->m_dwTextAlign |= XBFONT_TRUNCATED;
        }
      }
    }
  }

  return true;
}

bool CGUIDialog::OnAction(const CAction &action)
{
  if (action.wID == ACTION_CLOSE_DIALOG || action.wID == ACTION_PREVIOUS_MENU)
  {
    Close();
    return true;
  }
  return CGUIWindow::OnAction(action);
}

bool CGUIDialog::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
  case GUI_MSG_WINDOW_DEINIT:
    {
      CGUIWindow *pWindow = m_gWindowManager.GetWindow(m_gWindowManager.GetActiveWindow());
      if (pWindow && pWindow->GetOverlayState()!=OVERLAY_STATE_PARENT_WINDOW)
        m_gWindowManager.ShowOverlay(pWindow->GetOverlayState()==OVERLAY_STATE_SHOWN);
      break;
    }
  case GUI_MSG_WINDOW_INIT:
    {
      // set the initial fade state of the dialog
      if (m_effect.m_inTime)
      {
        if (m_effectState == EFFECT_OUT)
          m_effectStart = timeGetTime() - (int)(m_effect.m_inTime * m_attribute.alpha / 255.0f);
        else
          m_effectStart = timeGetTime();
        m_effectState = EFFECT_IN;
      }
      CGUIWindow::OnMessage(message);

      return true;
    }
  }

  return CGUIWindow::OnMessage(message);
}

void CGUIDialog::Close(bool forceClose /*= false*/)
{
  //Lock graphic context here as it is sometimes called from non rendering threads
  //maybe we should have a critical section per window instead??
  CSingleLock lock(g_graphicsContext);

  if (!m_bRunning) return;

  // don't close if we should be fading out
  if (!forceClose && m_effect.m_outTime)
  {
    if (m_effectState != EFFECT_OUT)
    {
      m_effectState = EFFECT_OUT;
      m_effectStart = timeGetTime() - (int)(m_effect.m_outTime * (255.0f - m_attribute.alpha) / 255.0f);
    }
    return;
  }

  //  Play the window specific deinit sound
  g_audioManager.PlayWindowSound(GetID(), SOUND_DEINIT);

  CGUIMessage msg(GUI_MSG_WINDOW_DEINIT, 0, 0);
  OnMessage(msg);

  if (m_bModal)
  {
    m_gWindowManager.UnRoute(GetID());
  }
  else
  {
    m_gWindowManager.RemoveModeless( GetID() );
  }

  m_pParentWindow = NULL;
  m_bRunning = false;
}

void CGUIDialog::DoModal(DWORD dwParentId, int iWindowID /*= WINDOW_INVALID */)
{
  //Lock graphic context here as it is sometimes called from non rendering threads
  //maybe we should have a critical section per window instead??
  CSingleLock lock(g_graphicsContext);

  m_dwParentWindowID = dwParentId;
  m_pParentWindow = m_gWindowManager.GetWindow( m_dwParentWindowID);

  if (!m_pParentWindow)
  {
    m_dwParentWindowID = 0;
    return ;
  }
  
  m_bModal = true;
  m_gWindowManager.RouteToWindow(this);

  //  Play the window specific init sound
  g_audioManager.PlayWindowSound(GetID(), SOUND_INIT);

  // active this window...
  CGUIMessage msg(GUI_MSG_WINDOW_INIT, 0, 0, WINDOW_INVALID, iWindowID);
  OnMessage(msg);

  m_bRunning = true;

  lock.Leave();
  while (m_bRunning)
  {
    m_gWindowManager.Process();
  }
}

void CGUIDialog::Show(DWORD dwParentId)
{
  //Lock graphic context here as it is sometimes called from non rendering threads
  //maybe we should have a critical section per window instead??
  CSingleLock lock(g_graphicsContext);

  if (m_bRunning && m_effectState != EFFECT_OUT) return;


  m_dwParentWindowID = dwParentId;
  m_pParentWindow = m_gWindowManager.GetWindow( m_dwParentWindowID);

  if (!m_pParentWindow)
  {
    m_dwParentWindowID = 0;
    return ;
  }

  m_bModal = false;
  m_gWindowManager.AddModeless(this);

  //  Play the window specific init sound
  g_audioManager.PlayWindowSound(GetID(), SOUND_INIT);

  // active this window...
  CGUIMessage msg(GUI_MSG_WINDOW_INIT, 0, 0);
  OnMessage(msg);

  m_bRunning = true;
}

void CGUIDialog::DoEffect(float effectAmount)
{
  if (m_effect.m_type == EFFECT_TYPE_FADE)
    m_attribute.alpha = (DWORD)(effectAmount * 255);
  else if (m_effect.m_type == EFFECT_TYPE_SLIDE)
  {
    float time = 1.0f - effectAmount;
    float amount = time * (m_effect.m_acceleration * time + 1.0f - m_effect.m_acceleration);
    m_attribute.offsetX = (int)(amount * (m_effect.m_startX - m_iPosX));
    m_attribute.offsetY = (int)(amount * (m_effect.m_startY - m_iPosY));
  }
}

void CGUIDialog::Render()
{
  DWORD currentTime = timeGetTime();
  if (m_effectState == EFFECT_IN)
  {
    if (currentTime - m_effectStart < m_effect.m_inDelay)
      DoEffect(0);
    else if (currentTime - m_effectStart < m_effect.m_inDelay + m_effect.m_inTime)
      DoEffect((float)(currentTime - m_effectStart - m_effect.m_inDelay) / m_effect.m_inTime);
    else
    {
      DoEffect(1);
      m_effectState = EFFECT_NONE;
    }
  }
  if (m_effectState == EFFECT_OUT)
  {
    if (currentTime - m_effectStart < m_effect.m_outDelay)
      DoEffect(1);
    else if (currentTime - m_effectStart < m_effect.m_outTime + m_effect.m_outDelay)
      DoEffect((float)(m_effect.m_outTime + m_effect.m_outDelay - currentTime + m_effectStart) / m_effect.m_outTime);
    else
    {
      DoEffect(0);
      m_effectState = EFFECT_NONE;
      Close(true);  // force the dialog to close
      return;
    }
  }
  CGUIWindow::Render();
}