param(
	[Parameter(Mandatory = $true)]
	[string]$OutPath
)

Add-Type -AssemblyName System.Speech

$OutPath = [System.IO.Path]::GetFullPath($OutPath)

$paragraphs = @(
	"The harbor town woke slowly that morning. Fishing boats rocked against the old stone wall while gulls argued over the scraps left behind by the night crew. A baker opened the shutters of her shop, lit the ovens, and began to knead the first batch of bread of the day. Down the street, a boy pushed a wheelbarrow full of crates toward the market square, whistling a tune he had learned from his grandfather.",
	"At the end of the pier stood a lighthouse that had guided ships for more than two hundred years. Its keeper, a quiet woman named Ada Morrow, climbed the spiral stairs every evening to check the lamp and polish the brass rail. She kept a logbook in neat handwriting, recording the weather, the tides, and every vessel that passed within sight of her window.",
	"Her logbook told a story of changing seasons. In spring, fog rolled in from the open sea and hid the horizon for days at a time. Summer brought long, bright evenings when the water shimmered like hammered silver. Autumn storms arrived with sudden fury, sending waves crashing over the breakwater, and winter nights were so cold that the lamp glass frosted over before midnight.",
	"One November evening, a radio message crackled through the speaker on the wall. A cargo ship called the Harriet Lane reported a broken steering gear and asked for guidance into the sheltered bay. Ada read the coordinates twice, checked the chart on her desk, and picked up the microphone. Her voice was calm and steady, the same voice she used to greet the fishermen each morning.",
	"The captain's reply came back in short, careful sentences. He had lost power to the rudder but still had a working engine. Ada explained the safest channel, the depth markers to watch for, and the place where the swell would be gentlest. For almost an hour the two of them talked, one from the stone tower and the other from a small steel bridge far out in the dark water.",
	"Meanwhile, the harbor master gathered two tugboats and a pilot launch. Their crews checked lines, tested engines, and dressed in bright orange jackets against the wind. Nobody panicked. Everyone knew the routine, and everyone trusted the woman in the lighthouse, who had never once given them a wrong bearing in all the years she had worked there.",
	"The sea was rough, but the ship came steadily through the channel. Ada watched through her telescope as the bow lights grew larger and the tugboats fell in alongside. When the Harriet Lane finally passed the breakwater and swung into the calm water of the bay, a cheer rose from the quay, and somewhere in the crowd a fisherman began to ring the old harbor bell.",
	"The next morning the newspaper printed a short article about the rescue, but it mentioned only the captain and the tugboat crews. Ada did not mind. She cut out a copy anyway and pinned it inside her logbook, between a page about a summer regatta and a page listing the first frost of the year.",
	"Her day continued as always. She cleaned the lenses, replaced a worn bulb in the signal lamp, and sent a report to the coast guard station about the depth of the new sandbar near the north buoy. In the afternoon, a group of school children visited the tower. They climbed the stairs in a long noisy line, pressed their faces against the glass, and asked a hundred questions about the sea.",
	"One girl asked whether the light could be seen from the other side of the world. Ada laughed and told her that the beam reached about twenty miles on a clear night, but the stars were much farther away, and their light had been travelling for longer than anyone could imagine. The girl thought about this for a long moment, then nodded seriously and wrote something down in her own small notebook.",
	"As evening came on, the wind shifted to the west and the air grew cooler. Ada lit the lamp, watched the beam sweep slowly across the water, and listened to the familiar sounds of the town below. A dog barked in the distance. A door closed. Someone laughed on the terrace of the inn, and the faint smell of woodsmoke drifted up from a dozen chimneys.",
	"Before she went down for the night, Ada wrote the final entry in her logbook. She recorded the wind direction, the barometer reading, the visibility, and the name of every ship that had passed. Then she added a single line of her own, a simple note that the harbor was safe and the lamp was burning, and she closed the book gently and turned out the light in the small room at the top of the stairs.",
	"Across the bay, the fish market opened at dawn under a tin roof that rattled whenever the wind came off the water. Traders carried baskets of silver herring and dark mussels past rows of wooden stalls, calling out prices in voices hoarse from the cold. A young clerk walked between them with a clipboard, marking down each sale and each shipment that came in on the early ferry from the islands.",
	"The ferry captain had made the crossing for thirty years and knew every shoal by the color of the water. On that Tuesday the sea was flat and gray, and gulls followed the wake for miles. He kept one hand on the wheel and the other around a mug of strong tea, chatting with a passenger who had come to visit a sister on the far shore and had never once seen the lighthouse from the water.",
	"In the workshop behind the church, an old clockmaker named Tobias Greer repaired pocket watches for the whole district. His bench was crowded with tiny gears, springs coiled like sleeping snakes, and jars of brass screws sorted by size. Each morning he wound the great regulator clock on the far wall, listened to its steady tick for a moment, and then turned to the day's first broken watch with the patience of a man who had never once been in a hurry.",
	"The schoolhouse stood on the hill above the harbor, its windows open to the breeze. Inside, the teacher wrote long division on a blackboard while children copied the numbers into their slates. Outside, a row of sycamores shook their leaves, and the bell in the tower rang out the hour so loudly that the chalk dust seemed to jump on the sill.",
	"Late in the afternoon a storm gathered over the eastern hills. Dark clouds piled up like bruised cotton, and the first heavy drops struck the cobblestones with a sound like coins. The farmers who had come into town for supplies hurried to their carts, covering sacks of flour and bundles of wool with oilcloth, while the shopkeepers pulled down their awnings and bolted their doors against the wind.",
	"The library at the end of Mill Street was small but full of surprises. Its shelves held travel journals from sea captains, faded maps of coastlines that no longer existed, and a shelf of handwritten recipes that the librarian guarded like treasure. Visitors were welcome to sit by the tall window and read as long as they liked, provided they returned each book to its proper place before leaving.",
	"Winter brought long quiet nights to the valley. Snow settled on the rooftops and muffled the sound of the river, and families gathered around stoves to share stories and mend clothing. Children tried to stay awake for midnight, and most of them failed, waking the next morning to find frost ferns drawn across the glass and the whole valley glittering under a pale blue sky.",
	"A traveling musician arrived at the inn in early spring with a battered violin case and a cough that refused to leave. He asked only for a room near the kitchen, where the stove was warm, and in return he played a few old songs each evening. The innkeeper's daughter sang along to the chorus, and before long the whole room was humming the same familiar melodies.",
	"The river ferry was pulled by a cable strung from bank to bank. When the current ran high, the operator had to lean into the crank and count the seconds between each turn of the wheel. On a clear day the crossing took less than five minutes, but in fog it could take twice as long, and passengers learned to keep their voices low so they could hear the bell on the far bank.",
	"Beyond the orchards, the old road climbed through a pass lined with pines. Travelers who took it in autumn were rewarded with views of red and gold valleys stretching toward the coast, and at the summit a small stone shelter offered a bench and a flask of cold water. Many carved their initials into its walls, and some left notes in a tin box for whoever came next.",
	"At the market the vendors sold everything from fresh cheese to hand-stitched leather gloves. A cart of ripe pears stood beside a table of copper pots, and a boy with a tray of hot chestnuts moved quickly between them, shouting that the chestnuts were still warm. Coins changed hands in a steady rhythm, and the air smelled of bread, smoke, and wet straw.",
	"The doctor who lived near the square was known for her long walks before breakfast. She carried a worn leather bag, a notebook for patient names, and a spare pair of spectacles in case the first pair broke. Her neighbors said she could diagnose a cough from across the street, and they were not entirely joking, because she had a remarkable ear for the sound of breathing.",
	"Every year, the town held a festival to celebrate the end of the harvest. Lanterns hung from the oak trees in the park, and a brass band played in the bandstand while couples danced on the grass. At midnight the mayor lit a bonfire on the beach, and the whole crowd sang the old fishing song that had been passed down through many generations of families.",
	"On the north coast, the cliffs were home to thousands of seabirds. Each spring they returned to their ledges, squabbling over the best nesting spots and filling the air with a noise that could be heard from the road. Local volunteers counted the nests every week and recorded the numbers in a ledger kept in a shed at the top of the path.",
	"The new bridge across the estuary took four years to build. Engineers had to sink massive concrete piers into soft mud, then raise steel arches high enough for tall ships to pass beneath. When the final section was lowered into place, workers gathered on both banks to watch, and a photographer on a rowboat captured the moment the two halves met in the middle.",
	"A small garden behind the post office belonged to a retired postman named Silas Brent. He grew tomatoes, beans, and a row of sunflowers taller than his shed. Every afternoon he sat on a wooden bench with a cup of coffee and watched the sparrows bathe in the shallow dish he had filled with rainwater, and he said it was the best part of his day.",
	"The lighthouse keeper's replacement arrived on a grey morning in late autumn. He was younger than Ada, with a nervous laugh and a stack of books about celestial navigation. She showed him the logbook, the brass rail, and the trick to lighting the lamp without burning his fingers. Then she handed him the key, climbed down the spiral stairs for the last time, and walked slowly home along the shore."
)

$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
try
{
	$voices = @($synth.GetInstalledVoices() | Where-Object { $_.Enabled -and $_.VoiceInfo.Culture.Name -like "en-*" } | ForEach-Object { $_.VoiceInfo.Name })
	if ($voices.Count -eq 0)
	{
		throw "no enabled English voice is installed"
	}
	$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(48000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
	$synth.SetOutputToWaveFile($OutPath, $format)
	for ($i = 0; $i -lt $paragraphs.Count; $i++)
	{
		$synth.SelectVoice($voices[$i % $voices.Count])
		$synth.Speak($paragraphs[$i])
	}
	$synth.SetOutputToNull()
	Write-Output ("voices: " + ($voices -join ", "))
	Write-Output ("wrote " + $OutPath)
}
finally
{
	$synth.Dispose()
}
