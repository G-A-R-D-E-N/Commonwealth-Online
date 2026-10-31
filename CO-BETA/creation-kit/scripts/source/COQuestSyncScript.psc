Scriptname COQuestSyncScript extends Quest

Bool primed

Event OnInit()
    Prime()
EndEvent

Event OnQuestInit()
    Prime()
EndEvent

Event Actor.OnPlayerLoadGame(Actor akSender)
    StartTimer(0.5, 1)
EndEvent

Function Prime()
    StartTimer(0.5, 1)
    If primed
        Return
    EndIf
    Actor player = Game.GetPlayer()
    If player
        primed = True
        RegisterForRemoteEvent(player, "OnPlayerLoadGame")
    EndIf
EndFunction

Event OnTimer(int aiTimerID)
    If aiTimerID != 1
        Return
    EndIf
    StartTimer(0.5, 1)

    If Utility.IsInMenuMode()
        ReportQuestStates()
        Return
    EndIf
    Actor player = Game.GetPlayer()
    If player
        If player.IsInScene()
            ReportQuestStates()
            Return
        EndIf
    EndIf

    While CommonwealthOnlineQuest.HasCommand()
        Quest target = Game.GetForm(CommonwealthOnlineQuest.NextCommandQuest()) as Quest
        Int stage = CommonwealthOnlineQuest.NextCommandStage()
        CommonwealthOnlineQuest.PopCommand()
        Bool applied = False
        If target
            If stage < 0
                target.CompleteQuest()
                applied = True
            Else
                applied = target.SetCurrentStageID(stage)
            EndIf
        EndIf
        CommonwealthOnlineQuest.ReportApplied(applied)
    EndWhile
    ReportQuestStates()
EndEvent

Function ReportQuestStates()
    Int count = CommonwealthOnlineQuest.QueryCount()
    Int index = 0
    While index < count
        Quest target = Game.GetForm(CommonwealthOnlineQuest.QueryQuest(index)) as Quest
        If target
            CommonwealthOnlineQuest.ReportState(target, target.IsCompleted(), target.IsRunning())
        EndIf
        index += 1
    EndWhile
EndFunction
