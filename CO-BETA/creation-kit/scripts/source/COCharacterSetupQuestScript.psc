Scriptname COCharacterSetupQuestScript extends Quest

InputEnableLayer Property SetupInputLayer Auto Hidden

Int Property TutorialFormId = 0x00160147 AutoReadOnly
Int Property MQ101FormId = 0x0001ED86 AutoReadOnly
Int Property MQ101PlayerCommentsFormId = 0x000F03D6 AutoReadOnly
Int Property MQ101SanctuaryHillsFormId = 0x000C3309 AutoReadOnly
Int Property MQ101Vault111FormId = 0x000D34DC AutoReadOnly
Int Property MQ101RadioFormId = 0x000BA4F3 AutoReadOnly
Int Property MQ101TVStationFormId = 0x000BA4F2 AutoReadOnly
Int Property CSMQ101CryopodFormId = 0x0019EE91 AutoReadOnly
Int Property CSMQ101PlayerHouseIntFormId = 0x00131B8D AutoReadOnly
Int Property MQ203DoorFormId = 0x00148B68 AutoReadOnly
Int Property AnimArchetypePlayerFormId = 0x0006B508 AutoReadOnly
Int Property AnimFaceArchetypePlayerFormId = 0x000D755E AutoReadOnly
Int Property FaceGenSinkRefTypeFormId = 0x00179D20 AutoReadOnly
Int Property TeleportOutSpellFormId = 0x001F1022 AutoReadOnly
Int Property TeleportInSpellFormId = 0x001E5601 AutoReadOnly

Function BeginVanillaOnboarding()
    Actor playerRef = Game.GetPlayer()
    Quest tutorial = VanillaForm(TutorialFormId) as Quest
    Quest mq101 = VanillaForm(MQ101FormId) as Quest

    If !playerRef || !tutorial || !mq101
        CommonwealthOnlineDebug.Trace("CharacterSetup", "BeginVanillaOnboarding skipped: a required form is missing")
        Return
    EndIf

    If mq101.GetStage() >= 900 || mq101.IsCompleted()
        CommonwealthOnlineDebug.Trace("CharacterSetup", "BeginVanillaOnboarding skipped: MQ101 is already past the vanilla intro")
        Return
    EndIf

    RegisterForRemoteEvent(playerRef, "OnGetUp")
    RegisterForRemoteEvent(tutorial, "OnStageSet")
    CommonwealthOnlineDebug.Trace("CharacterSetup", "BeginVanillaOnboarding registered for the FaceGen sink get-up and Tutorial stage 2025")
EndFunction

Event Actor.OnGetUp(Actor akSender, ObjectReference akFurniture)
    LocationRefType sinkType = VanillaForm(FaceGenSinkRefTypeFormId) as LocationRefType
    If !akFurniture || !sinkType || !akFurniture.HasRefType(sinkType)
        Return
    EndIf

    UnregisterForRemoteEvent(akSender, "OnGetUp")

    If !SetupInputLayer
        SetupInputLayer = InputEnableLayer.Create()
    EndIf
    If SetupInputLayer
        SetupInputLayer.DisablePlayerControls(abMovement = False, abFighting = True, abCamSwitch = True, abLooking = False, abSneaking = True, abMenu = False, abActivate = False, abJournalTabs = False, abVATS = False, abFavorites = False, abRunning = True)
    EndIf
    CommonwealthOnlineDebug.Trace("CharacterSetup", "OnGetUp at the FaceGen sink; player controls locked")
EndEvent

Event Quest.OnStageSet(Quest akSender, int auiStageID, int auiItemID)
    Quest tutorial = VanillaForm(TutorialFormId) as Quest
    If akSender != tutorial || auiStageID != 2025
        Return
    EndIf

    UnregisterForRemoteEvent(tutorial, "OnStageSet")
    Utility.Wait(1.0)
    RegisterForMenuOpenCloseEvent("SPECIALMenu")
    Game.ShowSPECIALMenu()

    ObjectReference magicDoor = VanillaForm(MQ203DoorFormId) as ObjectReference
    If magicDoor
        magicDoor.PreloadTargetArea()
    EndIf
    CommonwealthOnlineDebug.Trace("CharacterSetup", "Tutorial stage 2025 reached; SPECIAL menu opened")
EndEvent

Event OnMenuOpenCloseEvent(String asMenuName, Bool abOpening)
    If asMenuName == "SPECIALMenu" && !abOpening
        UnregisterForMenuOpenCloseEvent("SPECIALMenu")
        CommonwealthOnlineDebug.Trace("CharacterSetup", "SPECIAL menu closed")
    EndIf
EndEvent

Function ContinueToVault()
    Actor playerRef = Game.GetPlayer()
    Spell outSpell = VanillaForm(TeleportOutSpellFormId) as Spell
    Spell inSpell = VanillaForm(TeleportInSpellFormId) as Spell
    SoundCategorySnapshot cryopod = VanillaForm(CSMQ101CryopodFormId) as SoundCategorySnapshot
    ObjectReference magicDoor = VanillaForm(MQ203DoorFormId) as ObjectReference
    Quest mq101 = VanillaForm(MQ101FormId) as Quest

    If playerRef && outSpell && inSpell && cryopod && magicDoor && mq101
        Utility.Wait(1.0)
        playerRef.AddSpell(outSpell, False)
        Utility.Wait(3.0)
        cryopod.Push()
        magicDoor.Activate(playerRef)
        playerRef.AddSpell(inSpell, False)

        Var[] stageArgs = new Var[1]
        stageArgs[0] = 900
        mq101.CallFunctionNoWait("SetStage", stageArgs)

        Utility.Wait(0.5)
        Game.FadeOutGame(False, False, 12.0, 8.0)
        CommonwealthOnlineDebug.Trace("CharacterSetup", "ContinueToVault teleport sequence issued")
    Else
        CommonwealthOnlineDebug.Trace("CharacterSetup", "ContinueToVault skipped: the player or a required form is missing")
    EndIf
    Dispose()
EndFunction

Function Dispose()
    Actor playerRef = Game.GetPlayer()

    If SetupInputLayer
        SetupInputLayer.Delete()
        SetupInputLayer = None
    EndIf

    If playerRef
        playerRef.ChangeAnimArchetype(VanillaForm(AnimArchetypePlayerFormId) as Keyword)
        playerRef.ChangeAnimFaceArchetype(VanillaForm(AnimFaceArchetypePlayerFormId) as Keyword)
    EndIf

    SetVanillaQuestStage(MQ101PlayerCommentsFormId, 1000)
    SetVanillaQuestStage(MQ101SanctuaryHillsFormId, 1000)
    SetVanillaQuestStage(MQ101Vault111FormId, 1000)
    SetVanillaQuestStage(MQ101RadioFormId, 100)

    Quest tvStation = VanillaForm(MQ101TVStationFormId) as Quest
    If tvStation
        tvStation.Stop()
    EndIf

    SoundCategorySnapshot playerHouseInt = VanillaForm(CSMQ101PlayerHouseIntFormId) as SoundCategorySnapshot
    If playerHouseInt
        playerHouseInt.Remove()
    EndIf

    SetStage(100)
    CommonwealthOnlineDebug.Trace("CharacterSetup", "Dispose complete; CO_CharacterSetupQuest stage 100")
EndFunction

Function SetVanillaQuestStage(Int aiFormId, Int aiStage)
    Quest vanillaQuest = VanillaForm(aiFormId) as Quest
    If vanillaQuest
        vanillaQuest.SetStage(aiStage)
    EndIf
EndFunction

Function AbortOnboarding()
    Actor playerRef = Game.GetPlayer()
    Quest tutorial = VanillaForm(TutorialFormId) as Quest

    UnregisterForRemoteEvent(playerRef, "OnGetUp")
    UnregisterForRemoteEvent(tutorial, "OnStageSet")
    UnregisterForMenuOpenCloseEvent("SPECIALMenu")

    If SetupInputLayer
        SetupInputLayer.Delete()
        SetupInputLayer = None
    EndIf

    playerRef.ChangeAnimArchetype(VanillaForm(AnimArchetypePlayerFormId) as Keyword)
    playerRef.ChangeAnimFaceArchetype(VanillaForm(AnimFaceArchetypePlayerFormId) as Keyword)
    CommonwealthOnlineDebug.Trace("CharacterSetup", "AbortOnboarding complete")
EndFunction

Form Function VanillaForm(Int aiFormId)
    Return Game.GetFormFromFile(aiFormId, "Fallout4.esm")
EndFunction
