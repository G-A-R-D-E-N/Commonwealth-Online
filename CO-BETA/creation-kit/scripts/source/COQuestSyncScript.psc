Scriptname COQuestSyncScript extends Quest

Bool primed
Bool welcomeObjectiveHandled
Bool equipEventRegistered
Int hungerTier
Int thirstTier
Int sleepTier

Event OnInit()
    Prime()
EndEvent

Event OnQuestInit()
    Prime()
EndEvent

Event Actor.OnPlayerLoadGame(Actor akSender)
    welcomeObjectiveHandled = False
    StartTimer(0.5, 1)
EndEvent

Function Prime()
    StartTimer(0.5, 1)
    If primed && equipEventRegistered
        Return
    EndIf
    Actor player = Game.GetPlayer()
    If player
        If !primed
            primed = True
            RegisterForRemoteEvent(player, "OnPlayerLoadGame")
        EndIf
        If !equipEventRegistered
            equipEventRegistered = True
            RegisterForRemoteEvent(player, "OnItemEquipped")
        EndIf
    EndIf
EndFunction

Event Actor.OnItemEquipped(Actor akSender, Form akBaseObject, ObjectReference akReference)
    String kind = ConsumableKind(akBaseObject)
    If kind != ""
        CommonwealthOnlineNeeds.ReportConsume(akBaseObject, kind)
    EndIf
EndEvent

String Function ConsumableKind(Form akBaseObject)
    Potion item = akBaseObject as Potion
    If !item
        Return ""
    EndIf
    If item.IsHostile()
        Return ""
    EndIf
    If HasKeywordId(item, 0x0F4AEC) || HasKeywordId(item, 0x0F4AED) || HasKeywordId(item, 0x249F30) || HasKeywordId(item, 0x10C416) || HasKeywordId(item, 0x249F31)
        Return "drink"
    EndIf
    If HasKeywordId(item, 0x055ECC)
        Return "food"
    EndIf
    Return ""
EndFunction

Bool Function HasKeywordId(Form akForm, Int aiFormId)
    Keyword keywordForm = Game.GetFormFromFile(aiFormId, "Fallout4.esm") as Keyword
    If !keywordForm
        Return False
    EndIf
    Return akForm.HasKeyword(keywordForm)
EndFunction

Function UpdateNeedsFeedback()
    Int consumeResult = CommonwealthOnlineNeeds.TakeConsumeResult()
    If consumeResult == 1
        Debug.Notification("Hunger eased.")
    ElseIf consumeResult == 2
        Debug.Notification("Thirst quenched.")
    ElseIf consumeResult == 3
        Debug.Notification("You could not use that.")
    EndIf
    hungerTier = NotifyNeed(0, CommonwealthOnlineNeeds.GetHunger(), hungerTier)
    thirstTier = NotifyNeed(1, CommonwealthOnlineNeeds.GetThirst(), thirstTier)
    sleepTier = NotifyNeed(2, CommonwealthOnlineNeeds.GetSleep(), sleepTier)
EndFunction

Int Function NotifyNeed(Int aiNeed, Float afValue, Int aiLastTier)
    Int tier = 0
    If afValue >= 0.9
        tier = 3
    ElseIf afValue >= 0.75
        tier = 2
    ElseIf afValue >= 0.5
        tier = 1
    EndIf
    If tier > aiLastTier
        Debug.Notification(NeedText(aiNeed, tier))
    EndIf
    Return tier
EndFunction

String Function NeedText(Int aiNeed, Int aiTier)
    If aiNeed == 0
        If aiTier == 1
            Return "You are hungry."
        ElseIf aiTier == 2
            Return "You are very hungry."
        EndIf
        Return "You are starving."
    ElseIf aiNeed == 1
        If aiTier == 1
            Return "You are thirsty."
        ElseIf aiTier == 2
            Return "You are very thirsty."
        EndIf
        Return "You are dehydrated."
    EndIf
    If aiTier == 1
        Return "You are tired."
    ElseIf aiTier == 2
        Return "You are very tired."
    EndIf
    Return "You are exhausted."
EndFunction

Event OnTimer(int aiTimerID)
    If aiTimerID != 1
        Return
    EndIf
    StartTimer(0.5, 1)
    DisplayWelcomeObjective()

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
        Int kind = CommonwealthOnlineQuest.NextCommandKind()
        If kind == 1
            Quest creditTarget = Game.GetForm(CommonwealthOnlineQuest.NextCommandQuest()) as Quest
            Int objective = CommonwealthOnlineQuest.NextCommandObjective()
            String creditId = CommonwealthOnlineQuest.NextCommandCreditId()
            CommonwealthOnlineQuest.PopCommand()
            If creditTarget
                creditTarget.SetObjectiveCompleted(objective, True)
                If creditId != ""
                    CommonwealthOnlineQuest.ReportCreditApplied(creditId, creditTarget.GetStage())
                EndIf
            Else
                CommonwealthOnlineQuest.ReportApplied(False)
            EndIf
        Else
            Quest target = Game.GetForm(CommonwealthOnlineQuest.NextCommandQuest()) as Quest
            Int stage = CommonwealthOnlineQuest.NextCommandStage()
            CommonwealthOnlineQuest.PopCommand()
            Bool applied = False
            If target
                If !target.IsRunning() && !target.IsCompleted()
                    target.Start()
                EndIf
                If stage < 0
                    target.CompleteQuest()
                    applied = True
                Else
                    applied = target.SetCurrentStageID(stage)
                EndIf
            EndIf
            CommonwealthOnlineQuest.ReportApplied(applied)
        EndIf
    EndWhile
    UpdateNeedsFeedback()
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

Function RegisterCamp()
    ObjectReference benchRef = Game.FindClosestReferenceOfTypeFromRef(Game.GetFormFromFile(0x000C1AEB, "Fallout4.esm"), Game.GetPlayer(), 1024.0)
    WorkshopScript workshop = benchRef as WorkshopScript
    If !workshop
        Debug.Trace("COQuestSyncScript: RegisterCamp found no workshop bench near the player")
        Return
    EndIf
    WorkshopParentScript workshopParent = workshop.WorkshopParent
    If !workshopParent
        Debug.Trace("COQuestSyncScript: RegisterCamp found no WorkshopParent on the bench")
        Return
    EndIf
    WorkshopScript[] workshops = workshopParent.Workshops
    If workshops.Find(workshop) >= 0
        Return
    EndIf
    Location[] locations = workshopParent.WorkshopLocations
    Location campLocation = workshop.GetCurrentLocation()
    If !campLocation || workshops.Length != locations.Length
        Debug.Trace("COQuestSyncScript: RegisterCamp refused, the bench has no usable location")
        Return
    EndIf
    Int slot = locations.Find(campLocation)
    If slot >= 0
        If workshops[slot] && !workshops[slot].IsDeleted()
            Debug.Trace("COQuestSyncScript: RegisterCamp refused, the location belongs to a live workshop")
            Return
        EndIf
        workshops[slot] = workshop
        workshopParent.Workshops = workshops
    Else
        workshops.Add(workshop)
        locations.Add(campLocation)
        workshopParent.Workshops = workshops
        workshopParent.WorkshopLocations = locations
        slot = workshops.Length - 1
    EndIf
    workshop.InitWorkshopID(slot)
    workshop.myLocation = campLocation
    workshop.RegisterForCustomEvent(workshopParent, "WorkshopDailyUpdate")
    workshop.SetOwnedByPlayer(true)
EndFunction

Function DisplayWelcomeObjective()
    If welcomeObjectiveHandled
        Return
    EndIf
    Quest welcome = Game.GetFormFromFile(0x009981, "CommonwealthOnline.esp") as Quest
    If !welcome
        Return
    EndIf
    If welcome.GetStage() >= 20
        welcomeObjectiveHandled = True
    ElseIf welcome.GetStage() == 10
        welcome.SetObjectiveDisplayed(10, True)
        welcomeObjectiveHandled = True
    EndIf
EndFunction
