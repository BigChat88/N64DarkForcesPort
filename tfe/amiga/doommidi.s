;------------------------------------------------------------------------
; MIDI Synthesizer
; based on the Sound Library for DOOM - by Joseph Fenton
;------------------------------------------------------------------------

		include	"exec/exec.i"
		;include	"exec/funcdef.i"
		;include	"exec/exec_lib.i"
		include "lvo/exec_lib.i"
		include	"dos/dos.i"
		include	"dos/dos_lib.i"
		;include	"graphics/gfxbase.i"
		;include	"devices/audio.i"
		;include	"hardware/custom.i"
		;include	"hardware/intbits.i"
		;include	"hardware/dmabits.i"

		XREF _SysBase
		XREF _DOSBase
		XREF _kprintf

		XDEF _MIDI_LoadInstruments
		XDEF _MIDI_UnloadInstruments
		XDEF _MIDI_InitVolumeTable
		XDEF _MIDI_FreeVolumeTable
		XDEF _MIDI_FillBuffer
		XDEF _MIDI_AllNotesOff
		XDEF _MIDI_NoteOn
		XDEF _MIDI_NoteOff
		XDEF _MIDI_PitchBend
		XDEF _MIDI_ControlChange
		XDEF _MIDI_ProgramChange

CALLSYS			macro
		movea.l	_SysBase,a6
		jsr	(_LVO\1,a6)
		endm

CALLDOS			macro
		movea.l	_DOSBase,a6
		jsr	(_LVO\1,a6)
		endm

InstStr	dc.b	'instrument: %lu',10,0
OffsStr	dc.b	'offset: %lu',10,0
LengthStr	dc.b	'length: %lu',10,0

		CNOP	0,16

_MIDI_LoadInstruments	movem.l	d2-d7/a2-a6,-(sp)

		move.l	InstrFile,d1
		move.l	#MODE_OLDFILE,d2
		CALLDOS	Open
		move.l	d0,InstrHandle
		beq	.err0
		;bra .err0 ; TODO REMOVE
		move.l	#MEMF_CLEAR+MEMF_FAST+MEMF_PUBLIC,d0	    ; fast memory
		move.l	#65536,d1		; puddle size
		move.l	#32768,d2		; threshold size
		;bsr	CreatePool
		CALLSYS	CreatePool
		move.l	d0,InstrPool
		beq	.err1

		movea.l	d0,a2
		;movea.l	MusPtr,a3

		move.w	#255,d0
		lea	Instruments,a0
.setinstr	move.l	#QuietInst,(a0)+
		dbra	d0,.setinstr

		;move.w	$C(a3),d4		; instrCnt
		;ror.w	#8,d4
		;subq.w	#1,d4			; for dbra
		move.l	#181,d4			; 0-127 instruments, 135-181 percussion

		;lea	$10(a3),a3		; instruments[]
		moveq	#0,d1			; first instrument
		move.l	d1,a3

.instrloop	moveq	#14,d0
		movea.l	a2,a0
		;bsr	AllocPooled
		CALLSYS	AllocPooled

		;moveq	#0,d2
		;move.b	(a3)+,d2		; instrument #
		;moveq	#0,d1
		;move.b	(a3)+,d1		; offset to next instr. #
		;adda.l	d1,a3			; skip it (whatever it is?)
		move.l	a3,d2
		addq.l	#1,a3			; next instrument

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d2,-(sp)
		;pea InstStr
		;jsr _kprintf
		;addq.l #8,sp
		;movem.l	(sp)+,d0-d7/a0-a6
		;bra .next ; TODO REMOVE
		
		lea	Instruments,a0
		move.l	d0,(a0,d2.w*4)
		beq	.err2

		movea.l	d0,a4			; instrument record

		bftst	(validInstr){d2:1}
		beq	.next			; no instrument

		move.l	InstrHandle,d1
		lsl.l	#2,d2
		moveq	#OFFSET_BEGINNING,d3
		CALLDOS	Seek

		move.l	InstrHandle,d1
		move.l	a4,d2
		moveq	#4,d3
		CALLDOS	Read			; get instrument offset
		addq.l	#1,d0
		beq	.err2			; can't read file

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l (a4),-(sp)
		;pea OffsStr
		;jsr _kprintf
		;addq.l #8,sp
		;movem.l	(sp)+,d0-d7/a0-a6

		move.l	InstrHandle,d1
		move.l	(a4),d2
		moveq	#OFFSET_BEGINNING,d3
		CALLDOS	Seek

		move.l	InstrHandle,d1
		move.l	a4,d2
		moveq	#14,d3
		CALLDOS	Read			; get instrument header
		addq.l	#1,d0
		beq	.err2			; can't read file

		move.l	in_Length(a4),d0
		swap	d0
		movea.l	a2,a0
		;bsr	AllocPooled
		CALLSYS	AllocPooled
		move.l	d0,in_Wave(a4)		; wave data buffer
		beq	.err2

		move.l	InstrHandle,d1
		move.l	d0,d2
		move.l	in_Length(a4),d3
		swap	d3
		CALLDOS	Read			; get instrument samples
		addq.l	#1,d0
		beq	.err2			; can't read file

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d3,-(sp)
		;pea LengthStr
		;jsr _kprintf
		;addq.l #8,sp
		;movem.l	(sp)+,d0-d7/a0-a6

		move.l	in_Loop(a4),d0
		move.l	in_Length(a4),d1
		lsr.l	#8,d0
		lsr.l	#8,d1
		move.l	d0,in_Loop(a4)
		move.l	d1,in_Length(a4)

		move.b	#1,in_Flags(a4)
.next		dbra	d4,.instrloop

		move.l	InstrHandle,d1
		CALLDOS	Close
		clr.l	InstrHandle

		moveq	#1,d0			; return handle=1
		movem.l	(sp)+,d2-d7/a2-a6
		rts

.err2		movea.l	InstrPool,a0
		;bsr	DeletePool
		CALLSYS	DeletePool
		clr.l	InstrPool

.err1		move.l	InstrHandle,d1
		CALLDOS	Close
		clr.l	InstrHandle

.err0		moveq	#0,d0			; return handle=0
		movem.l	(sp)+,d2-d7/a2-a6
		rts

;--------------------------------------

		CNOP	0,16

_MIDI_UnloadInstruments	movem.l	d2-d7/a2-a6,-(sp)

		tst.l	InstrPool
		beq.b	.1
		movea.l	InstrPool,a0
		;bsr	DeletePool
		CALLSYS	DeletePool
		clr.l	InstrPool

.1		movem.l	(sp)+,d2-d7/a2-a6
		rts

;--------------------------------------------------------------------

VolStr	dc.b	'volume: %lu',10,0

		CNOP	0,16

_MIDI_InitVolumeTable	movem.l	d2-d7/a2-a6,-(sp)

.2		move.l	#128*256,d0
		move.l	#MEMF_CLEAR+MEMF_FAST+MEMF_PUBLIC,d1
		CALLSYS	AllocVec
		move.l	d0,LookupMem
		beq	.err0			; no memory for vol_lookup
		move.l	d0,vol_lookup
		movea.l	d0,a0

		moveq	#0,d2
		moveq	#0,d1
.3		move.b	d1,d3
		ext.w	d3
		muls.w	d2,d3
		divs.w	#127,d3			; i*j/127
		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d3,-(sp)
		;pea VolStr
		;jsr _kprintf
		;addq.l #8,sp
		;movem.l	(sp)+,d0-d7/a0-a6
		move.b	d3,(a0)+
		addq.b	#1,d1
		bne.b	.3
		addq.b	#1,d2
		cmpi.b	#128,d2
		bne.b	.3

		movem.l	(sp)+,d2-d7/a2-a6
		moveq	#1,d0		; okay
		rts

.err0		movem.l	(sp)+,d2-d7/a2-a6
		moveq	#0,d0		; error
		rts

;--------------------------------------

		CNOP	0,16

_MIDI_FreeVolumeTable	movem.l	d2-d7/a2-a6,-(sp)

		movea.l	LookupMem,a1
		CALLSYS	FreeVec

		movem.l	(sp)+,d2-d7/a2-a6
		;moveq	#0,d0
		rts

;------------------------------------------------------------------------

FillBufferDbg	dc.b	'buffer %lx samples %lu',10,0
FillBufferDbg2	dc.b	'test %lu',10,0
FillBufferReleasing	dc.b	'releasing %lx vc_LtVol %lu vc_RtVol %lu',10,0
FillBufferHolding	dc.b	'voice %lx channel %lx vc_LtVol %4lu vc_RtVol %4lu VoiceAvail %lx',10,0
FillBufferHanging	dc.b	'released hanging voice %2lu note %3lu',10,0
FillBufferReleased	dc.b	'released oneshot voice %lu note %lu',10,0

		CNOP	0,16

; clobbers: d2, d3, d4, d5, d6, d7
; clobbers: a2~, a3, a4~, a5

; a0 = starting voice
; a2 = buffer (256 bytes) movea.l	chip_buffer,a2
_MIDI_FillBuffer

			rsreset
.intregs	rs.l	10
			rs.l	1
.buffer		rs.l	1
.samples	rs.l	1

		movem.l	d2-d7/a2-a5,-(sp)

		;lea	.buffer(sp),a2		; sample buffer
		move.l	.buffer(sp),a2		; sample buffer
		move.l	.samples(sp),d0
		subq.w	#1,d0			; for dbra
		move.l	d0,a4			; sample count

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d0,-(sp)
		;move.l a2,-(sp)
		;pea FillBufferDbg
		;jsr _kprintf
		;lea (12,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6


		;move.l	.samples(sp),d1
;		movea.l	a2,a4
;		lea	tempAudio,a1
;		moveq	#4,d0
;.lcloop		clr.l	(a1)+
;		clr.l	(a1)+
;		clr.l	(a1)+
;		clr.l	(a1)+
;		dbra	d0,.lcloop

;		lea	tempAudio+128,a1
;		moveq	#4,d0
;.rcloop		clr.l	(a1)+
;		clr.l	(a1)+
;		clr.l	(a1)+
;		clr.l	(a1)+
;		dbra	d0,.rcloop

; TODO ENABLE
		lea	Voice0,a0
		bra.b	.1stvoice
; TODO ENABLE
.next		move.l	vc_Next(a0),d0		; next voice
		bne.b	.chkvoice

;		lea	128(a4),a2
;		lea	tempAudio,a1
;		moveq	#4,d0
;.lmloop		move.l	(a1)+,(a4)+
;		move.l	(a1)+,(a4)+
;		move.l	(a1)+,(a4)+
;		move.l	(a1)+,(a4)+
;		dbra	d0,.lmloop				; 4*4*4 = 64

;		lea	tempAudio+128,a1
;		moveq	#4,d0
;.rmloop		move.l	(a1)+,(a2)+
;		move.l	(a1)+,(a2)+
;		move.l	(a1)+,(a2)+
;		move.l	(a1)+,(a2)+
;		dbra	d0,.rmloop
		movem.l	(sp)+,d2-d7/a2-a5
		rts

.chkvoice	movea.l	d0,a0
.1stvoice	btst	#0,vc_Flags(a0)
		beq.b	.next			; not enabled

;------------------
; do voice
;		btst	#7,vc_Flags(a0)
;		bne	.5			; sfx

		btst	#1,vc_Flags(a0)
		beq.b	.1			; not releasing

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l vc_RtVol(a0),-(sp)
		;move.l vc_LtVol(a0),-(sp)
		;move.l a0,-(sp)
		;pea FillBufferReleasing
		;jsr _kprintf
		;lea (16,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		tst.l	vc_LtVol(a0)
		beq.b	.0
		subi.l	#256,vc_LtVol(a0)

.0		tst.l	vc_RtVol(a0)
		beq.b	.1
		subi.l	#256,vc_RtVol(a0)

.1

; hanging note detection start

		;bra.b	.nohang			; TODO remove
		tst.l	vc_Loop(a0)
		beq.b	.nohang			; no looping
		movea.l	vc_Channel(a0),a3		; channel record
		movea.l	ch_Map(a3),a3		; channel map
		clr.l	d3
		move.b	vc_Note(a0),d3		; note #
		clr.l	d2
		move.b	vc_Num(a0),d2
		cmp.b	(a3,d3.w),d2
		beq.b	.nohang			; the voice is still in the channel map
		;clr.l	vc_LtVol(a0)	; left volume off
		;clr.l	vc_RtVol(a0)	; right volume off
		bset	#1,vc_Flags(a0)		; do release
		;bra.b	.nohang			; TODO remove
		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d3,-(sp)
		;move.l d2,-(sp)
		;pea FillBufferHanging
		;jsr _kprintf
		;lea (12,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

.nohang

; hanging note detection end

		move.l	vc_LtVol(a0),d6
		move.l	vc_RtVol(a0),d7
		tst.l	d6
		bne.b	.2			; not off yet
		tst.l	d7
		bne.b	.2			; not off yet

		clr.b	vc_Flags(a0)		; voice off
		clr.l	d2
		move.b	vc_Num(a0),d2
		move.l	VoiceAvail,d3
		bclr	d2,d3			; voice free for use
		move.l	d3,VoiceAvail

		bra	.next

;.2		lea	tempAudio,a1		; left buffer
.2
; DEBUG PRINT START
;		movea.l	vc_Channel(a0),a3
;		movem.l	d0-d7/a0-a6,-(sp)
;		move.l VoiceAvail,-(sp)
;		move.l vc_RtVol(a0),-(sp)
;		move.l vc_LtVol(a0),-(sp)
;		move.l a3,-(sp)
;		move.l a0,-(sp)
;		pea FillBufferHolding
;		jsr _kprintf
;		lea (24,sp),sp
;		movem.l	(sp)+,d0-d7/a0-a6
; DEBUG PRINT END

		movea.l	a2,a1		; left buffer
		movea.l	vol_lookup,a5

		movem.l	vc_Index(a0),d1-d4	; d1 vc_Index, d2 vc_Step, d3 vc_Loop, d4 vc_Length

		;movea.l	vc_Channel(a0),a3
		;move.l	ch_Pitch(a3),d0
		;muls.l	d0,d5:d2			; TODO 68060 version
		;move.w	d5,d2
		;swap	d2
		;add.l	vc_Step(a0),d2		; final sample rate
		;move.l	vc_Step(a0),d2		; final sample rate
		lsr.l	#8,d2

		movea.l	vc_Wave(a0),a3		; sample data

;		moveq	#79,d5			; 80 samples
		move.l	a4,d5			; sample count
.floop		move.l	d1,d0
		lsr.l	#8,d0
		move.b	(a3,d0.l),d6		; sample
		move.b	d6,d7
		move.b	(a5,d6.l),d6		; convert
		move.b	(a5,d7.l),d7		; convert
		asr.b	#2,d6
		asr.b	#2,d7
		add.b	d6,(a1)+		; left sample
		;add.b	d7,127(a1)		; right sample
		add.b	d7,(a1)+		; right sample

		add.l	d2,d1
		cmp.l	d4,d1
		blo.b	.3
		sub.l	d4,d1
		add.l	d3,d1
		tst.l	d3
		beq.b	.4			; no looping
.3		dbra	d5,.floop
		bra.b	.done			; done with voice

; ran out of data
.4		clr.b	vc_Flags(a0)		; voice off
		clr.l	d2
		move.b	vc_Num(a0),d2
		move.l	VoiceAvail,d3
		bclr	d2,d3			; voice free for use
		move.l	d3,VoiceAvail

.done		move.l	d1,vc_Index(a0)
		bra	.next

;------------------------------------------------------------------------

		CNOP	0,16

_MIDI_AllNotesOff

		;move.l	#QuietInst,Channel0
		;move.l	#QuietInst,Channel1
		;move.l	#QuietInst,Channel2
		;move.l	#QuietInst,Channel3
		;move.l	#QuietInst,Channel4
		;move.l	#QuietInst,Channel5
		;move.l	#QuietInst,Channel6
		;move.l	#QuietInst,Channel7
		;move.l	#QuietInst,Channel8
		;move.l	#QuietInst,Channel9
		;move.l	#QuietInst,Channel10
		;move.l	#QuietInst,Channel11
		;move.l	#QuietInst,Channel12
		;move.l	#QuietInst,Channel13
		;move.l	#QuietInst,Channel14
		;move.l	#QuietInst,Channel15
		; TODO ch_Map

		clr.b	Voice0+vc_Flags		; disable voices
		clr.b	Voice1+vc_Flags
		clr.b	Voice2+vc_Flags
		clr.b	Voice3+vc_Flags
		clr.b	Voice4+vc_Flags
		clr.b	Voice5+vc_Flags
		clr.b	Voice6+vc_Flags
		clr.b	Voice7+vc_Flags
		clr.b	Voice8+vc_Flags
		clr.b	Voice9+vc_Flags
		clr.b	Voice10+vc_Flags
		clr.b	Voice11+vc_Flags
		clr.b	Voice12+vc_Flags
		clr.b	Voice13+vc_Flags
		clr.b	Voice14+vc_Flags
		clr.b	Voice15+vc_Flags
		clr.b	Voice16+vc_Flags
		clr.b	Voice17+vc_Flags
		clr.b	Voice18+vc_Flags
		clr.b	Voice19+vc_Flags
		clr.b	Voice20+vc_Flags
		clr.b	Voice21+vc_Flags
		clr.b	Voice22+vc_Flags
		clr.b	Voice23+vc_Flags
		clr.b	Voice24+vc_Flags
		clr.b	Voice25+vc_Flags
		clr.b	Voice26+vc_Flags
		clr.b	Voice27+vc_Flags
		clr.b	Voice28+vc_Flags
		clr.b	Voice29+vc_Flags
		clr.b	Voice30+vc_Flags
		clr.b	Voice31+vc_Flags

		clr.l	VoiceAvail		; all voices available
		rts

;------------------------------------------------------------------------

NoteOff1Str	dc.b	'NoteOff channel %lu ',0
NoteOff2Str	dc.b	'note %lu',10,0
NoteOffFreeStr	dc.b	'freeing voice %lu',10,0
NoteOffVoiceStr	dc.b	'voiceavail %lx',10,0

		CNOP	0,16

; clobbers: d0, d1, d2, d3
;  d0  = channel
; (a1) = note # -> key | 0x80
_MIDI_NoteOff

			rsreset
.intregs	rs.l	2
			rs.l	1
.channel	rs.l	1
.note		rs.l	1

		movem.l	d2/d3,-(sp)

		moveq	#15,d1
		and.l	.channel(sp),d1			; d1 = channel

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d1,-(sp)
		;pea NoteOff1Str
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		lea	Channels,a0
		movea.l	(a0,d1.w*4),a0		; channel record
		movea.l	ch_Map(a0),a0		; channel map

		move.l	.note(sp),d1		; note #
		
		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d1,-(sp)
		;pea NoteOff2Str
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6
		
		moveq	#0,d2
		move.b	(a0,d1.w),d2		; voice #
		
		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d2,-(sp)
		;pea NoteOffFreeStr
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		;beq.b	.exit			; no mapping
		bmi.b .exit

		;clr.b	(a0,d1.w)		; clear mapping
		st.b	(a0,d1.w)		; clear mapping to 255
		move.l	VoiceAvail,d3
		bclr	d2,d3			; voice free for use
		move.l	d3,VoiceAvail

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d3,-(sp)
		;pea NoteOffVoiceStr
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		lea	Voices,a0
		movea.l	(a0,d2.w*4),a0		; voice
		bset	#1,vc_Flags(a0)		; do release

.exit	movem.l	(sp)+,d2/d3
		rts

;------------------------------------------------------------------------

NoteOn1Str	dc.b	'NoteOn channel %lu ',0
NoteOn2Str	dc.b	'no free voice, note %lu volume %lu',10,0
NoteOnFreeStr	dc.b	'found voice %lu',10,0
NoteOnVoiceStr	dc.b	'voiceavail %lx',10,0
NoteOnLVolStr	dc.b	'left vol %lu',10,0
NoteOnRVolStr	dc.b	'right vol %lu',10,0

		CNOP	0,16

; clobbers: d2, d3, d4
; clobbers: a2, a3, a4
;  d0 = channel
; (a1) = note # -> key | 0x80
;      volume -> volocity?
_MIDI_NoteOn

			rsreset
.intregs	rs.l	6
			rs.l	1
.channel	rs.l	1
.note		rs.l	1
.velocity	rs.l	1

		movem.l	d2-d4/a2-a4,-(sp)

		moveq	#15,d1
		and.l	.channel(sp),d1			; d1 = channel

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d1,-(sp)
		;pea NoteOn1Str
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		lea	Channels,a0
		movea.l	(a0,d1.w*4),a2		; channel record
		movea.l	ch_Map(a2),a0		; channel map

		;moveq	#-1,d2			; no volume change
		move.l	.note(sp),d1		; note #
		;bclr	#7,d1
		;beq.b	.getvc			; no volume

		;moveq	#0,d2
		move.l	.velocity(sp),d2		; volume

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d2,-(sp)
		;move.l d1,-(sp)
		;pea NoteOn2Str
		;jsr _kprintf
		;lea (12,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

.getvc		moveq	#0,d3
		move.l	VoiceAvail,d4
.vloop		bset	d3,d4
		beq.b	.foundfree
		addq.b	#1,d3
		cmpi.b	#16,d3
		bne.b	.vloop
; no free voices
		movem.l	d0-d7/a0-a6,-(sp)
		move.l d2,-(sp)
		move.l d1,-(sp)
		pea NoteOn2Str
		jsr _kprintf
		lea (12,sp),sp
		movem.l	(sp)+,d0-d7/a0-a6
		movem.l	(sp)+,d2-d4/a2-a4
		rts


.foundfree
		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d3,-(sp)
		;pea NoteOnFreeStr
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		move.b	d3,(a0,d1.w)		; voice mapping
		move.l	d4,VoiceAvail
		
		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d4,-(sp)
		;pea NoteOnVoiceStr
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		lea	Voices,a0
		movea.l	(a0,d3.w*4),a3		; voice

		;tst.b	d2
		;bmi.b	.skip

; new channel volume

		move.b	d2,ch_Vol(a2)
		moveq	#0,d3
		move.b	ch_Pan(a2),d3
		addq.w	#1,d3		; sep += 1
		move.w	d2,d4
		muls.w	d3,d4
		muls.w	d3,d4
		clr.w	d4
		swap	d4
		neg.w	d4
		add.w	d2,d4		; ltvol = vol - vol * (sep+1)^2 / 256^2
		lsl.l	#8,d4
		move.l	d4,ch_LtVol(a2)
		
		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d4,-(sp)
		;pea NoteOnLVolStr
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		subi.l	#257,d3		; sep -= 257
		move.w	d2,d4
		muls.w	d3,d4
		muls.w	d3,d4
		clr.w	d4
		swap	d4
		neg.w	d4
		add.w	d2,d4		; rtvol = vol - vol * (sep-257)^2 / 256^2
		lsl.l	#8,d4
		move.l	d4,ch_RtVol(a2)
		
		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d4,-(sp)
		;pea NoteOnRVolStr
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

.skip		move.l	ch_LtVol(a2),vc_LtVol(a3)
		move.l	ch_RtVol(a2),vc_RtVol(a3)

		moveq	#15,d2
		and.b	d0,d2
		;cmpi.b	#15,d2
		cmpi.b	#9,d2				; channel 9 is percussion
		beq.b	.percussion

		move.l	ch_Instr(a2),a4		; instrument record

		lea	NoteTable,a0
		moveq	#72,d2			; one octave above middle c
		sub.b	in_Base(a4),d2
		add.b	d1,d2
		move.l	(a0,d2.w*4),vc_Step(a3)	; step value for note

		clr.l	vc_Index(a3)

		move.l	a2,vc_Channel(a3)	; channel back link
		move.b	d1,vc_Note(a3)		; note back link

		move.l	in_Wave(a4),vc_Wave(a3)
		move.l	in_Loop(a4),vc_Loop(a3)
		move.l	in_Length(a4),vc_Length(a3)
		move.b	in_Flags(a4),vc_Flags(a3)
.exit	movem.l	(sp)+,d2-d4/a2-a4
		rts

; for the percussion channel, the note played sets the percussion instrument

.percussion	move.l	#65536,vc_Step(a3)	; sample rate always 1.0

		clr.l	vc_Index(a3)

		move.l	a2,vc_Channel(a3)	; channel back link
		move.b	d1,vc_Note(a3)		; note back link

		addi.b	#100,d1			; percussion instruments

		lea	Instruments,a0
		move.l	(a0,d1.w*4),a0		; instrument record
		move.l	in_Wave(a0),vc_Wave(a3)
		move.l	in_Loop(a0),vc_Loop(a3)
		move.l	in_Length(a0),vc_Length(a3)
		move.b	in_Flags(a0),vc_Flags(a3)
		movem.l	(sp)+,d2-d4/a2-a4
		rts

;------------------------------------------------------------------------

		CNOP	0,16

; clobbers: a2
;  d0  = channel
; (a1) = (currevent->param1 | currevent-> param2 << 7) >> 6;
_MIDI_PitchBend

			rsreset
.intregs	rs.l	1
			rs.l	1
.channel	rs.l	1
.pitch		rs.l	1

		move.l	a2,-(sp)

		moveq	#15,d1
		and.l	.channel(sp),d1			; d1 = channel

		lea	Channels,a0
		movea.l	(a0,d1.w*4),a2		; channel record

		;moveq	#0,d1
		move.l	.pitch(sp),d1		; pitch wheel setting
		lea	PitchTable,a0
		move.l	(a0,d1.w*4),ch_Pitch(a2)

		move.l	(sp)+,a2
		rts

;------------------------------------------------------------------------

ControlChange1Str	dc.b	'ControlChange channel %lu ',0
ControlChange2Str	dc.b	'controller %lu value %lu',10,0
ControlChangeVoice1	dc.b	'check  voice %lx',10,0
ControlChangeVoice2	dc.b	'update voice %lx',10,0

		CNOP	0,16

; clobbers: d2, d3, d4
; clobbers: a2
;  d0  = channel
; (a1) = Control #
;        value
_MIDI_ControlChange

			rsreset
.intregs	rs.l	4
			rs.l	1
.channel	rs.l	1
.controller	rs.l	1
.value		rs.l	1

		movem.l	d2-d4/a2,-(sp)

		moveq	#15,d1
		and.l	.channel(sp),d1			; d1 = channel

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d1,-(sp)
		;pea ControlChange1Str
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		lea	Channels,a0
		movea.l	(a0,d1.w*4),a2		; channel

		move.l	.controller(sp),d1		; get controller

		;moveq	#0,d2
		move.l	.value(sp),d2		; value

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d2,-(sp)
		;move.l d1,-(sp)
		;pea ControlChange2Str
		;jsr _kprintf
		;lea (12,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

;.1		cmpi.b	#3,d1			; volume?
.1		cmpi.b	#7,d1			; volume?
		bne.b	.2

; set channel volume

		move.b	d2,ch_Vol(a2)
		bra.b	.updatechan

;.2		cmpi.b	#4,d1			; pan?
.2		cmpi.b	#10,d1			; pan?
		bne.w	.exit ; todo bne.b

; set channel pan

		add.b	d2,d2		; pan -> sep
		move.b	d2,ch_Pan(a2)
		move.b	ch_Vol(a2),d2

.updatechan

		moveq	#0,d3
		move.b	ch_Pan(a2),d3
		addq.w	#1,d3		; sep += 1
		move.w	d2,d4
		muls.w	d3,d4
		muls.w	d3,d4
		clr.w	d4
		swap	d4
		neg.w	d4
		add.w	d2,d4		; ltvol = vol - vol * (sep+1)^2 / 256^2
		lsl.l	#8,d4
		move.l	d4,ch_LtVol(a2)

		subi.w	#257,d3		; sep -= 257
		move.w	d2,d4
		muls.w	d3,d4
		muls.w	d3,d4
		clr.w	d4
		swap	d4
		neg.w	d4
		add.w	d2,d4		; rtvol = vol - vol * (sep-257)^2 / 256^2
		lsl.l	#8,d4
		move.l	d4,ch_RtVol(a2)

; update the voices

		lea	Voice0,a0
		bra.b	.1stvoice
.next		move.l	vc_Next(a0),a0		; next voice
		beq.b	.exit
.1stvoice
		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l a0,-(sp)
		;pea ControlChangeVoice1
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

		btst	#0,vc_Flags(a0)
		beq.b	.next			; not enabled
		move.l	vc_Channel(a0),d0
		cmp.l	a2,d0
		bne.b	.next		; not on this channel

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l a0,-(sp)
		;pea ControlChangeVoice2
		;jsr _kprintf
		;lea (8,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

; adjust the volume

		move.l	ch_LtVol(a2),vc_LtVol(a0)
		move.l	ch_RtVol(a2),vc_RtVol(a0)

		bra.b	.next


.exit
		movem.l	(sp)+,d2-d4/a2
		rts

;------------------------------------------------------------------------

ProgramChangeStr	dc.b	'ProgramChange channel %lu value %lu',10,0

		CNOP	0,16

; clobbers: d2
; clobbers: a2
;  d0  = channel
; (a1) = value
_MIDI_ProgramChange

			rsreset
.intregs	rs.l	2
			rs.l	1
.channel	rs.l	1
.value		rs.l	1

		movem.l	d2/a2,-(sp)

		moveq	#15,d1
		and.l	.channel(sp),d1			; d1 = channel
		cmpi.b	#9,d1				; channel 9 is percussion
		beq.b	.0

		lea	Channels,a0
		movea.l	(a0,d1.w*4),a2		; channel

		;moveq	#0,d2
		move.l	.value(sp),d2		; value

		;movem.l	d0-d7/a0-a6,-(sp)
		;move.l d2,-(sp)
		;move.l d1,-(sp)
		;pea ProgramChangeStr
		;jsr _kprintf
		;lea (12,sp),sp
		;movem.l	(sp)+,d0-d7/a0-a6

; set channel instrument

		lea	Instruments,a0
		move.l	(a0,d2.w*4),ch_Instr(a2)
		;bne.b	.0
		;move.l	#QuietInst,ch_Instr(a2)
.0		movem.l	(sp)+,d2/a2
		rts

;------------------------------------------------------------------------

		cnop	0,4

;MUSMemPtr	dc.l	0
;MUSMemSize	dc.l	0

;SegList		dc.l	0
;_ExecBase	dc.l	0
;_DOSBase	dc.l	0
LookupMem	dc.l	0
vol_lookup	dc.l	0

;--------------------------------------

		CNOP	0,4

; bit set if voice is in use (0-15=music voices,16-31=sfx voices)

VoiceAvail	dc.l	0

;chip_buffer	dc.l	chipBuffer
;chip_offset	dc.l	512

;--------------------------------------------------------------------

		CNOP	0,4

NoteTable	dc.l	65536/64,69433/64,73562/64,77936/64,82570/64,87480/64,92682/64,98193/64,104032/64,110218/64,116772/64,123715/64
		dc.l	65536/32,69433/32,73562/32,77936/32,82570/32,87480/32,92682/32,98193/32,104032/32,110218/32,116772/32,123715/32
		dc.l	65536/16,69433/16,73562/16,77936/16,82570/16,87480/16,92682/16,98193/16,104032/16,110218/16,116772/16,123715/16
		dc.l	65536/8,69433/8,73562/8,77936/8,82570/8,87480/8,92682/8,98193/8,104032/8,110218/8,116772/8,123715/8
		dc.l	65536/4,69433/4,73562/4,77936/4,82570/4,87480/4,92682/4,98193/4,104032/4,110218/4,116772/4,123715/4
		dc.l	65536/2,69433/2,73562/2,77936/2,82570/2,87480/2,92682/2,98193/2,104032/2,110218/2,116772/2,123715/2
		dc.l	65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715
		dc.l	65536*2,69433*2,73562*2,77936*2,82570*2,87480*2,92682*2,98193*2,104032*2,110218*2,116772*2,123715*2
		dc.l	65536*4,69433*4,73562*4,77936*4,82570*4,87480*4,92682*4,98193*4,104032*4,110218*4,116772*4,123715*4
		dc.l	65536*8,69433*8,73562*8,77936*8,82570*8,87480*8,92682*8,98193*8,104032*8,110218*8,116772*8,123715*8
		dc.l	65536*16,69433*16,73562*16,77936*16,82570*16,87480*16,92682*16,98193*16

;------------------------------------------------------------------------

PitchTable:

pitch_ix	SET	128

		REPT	128
		dc.l	-3678*pitch_ix/64
pitch_ix	SET	pitch_ix-1
		ENDR

		REPT	128
		dc.l	3897*pitch_ix/64
pitch_ix	SET	pitch_ix+1
		ENDR

;------------------------------------------------------------------------

		STRUCTURE MusChannel,0
		APTR	ch_Instr
		APTR	ch_Map
		ULONG	ch_Pitch
		ULONG	ch_LtVol
		ULONG	ch_RtVol
		BYTE	ch_Vol
		BYTE	ch_Pan


		CNOP	0,4

Channels	dc.l	Channel0,Channel1,Channel2,Channel3
		dc.l	Channel4,Channel5,Channel6,Channel7
		dc.l	Channel8,Channel9,Channel10,Channel11
		dc.l	Channel12,Channel13,Channel14,Channel15


		CNOP	0,4

Channel0	dc.l	0		; instrument
		dc.l	Channel0Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel1	dc.l	0		; instrument
		dc.l	Channel1Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel2	dc.l	0		; instrument
		dc.l	Channel2Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel3	dc.l	0		; instrument
		dc.l	Channel3Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel4	dc.l	0		; instrument
		dc.l	Channel4Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel5	dc.l	0		; instrument
		dc.l	Channel5Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel6	dc.l	0		; instrument
		dc.l	Channel6Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel7	dc.l	0		; instrument
		dc.l	Channel7Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel8	dc.l	0		; instrument
		dc.l	Channel8Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel9	dc.l	0		; instrument
		dc.l	Channel9Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel10	dc.l	0		; instrument
		dc.l	Channel10Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel11	dc.l	0		; instrument
		dc.l	Channel11Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel12	dc.l	0		; instrument
		dc.l	Channel12Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel13	dc.l	0		; instrument
		dc.l	Channel13Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel14	dc.l	0		; instrument
		dc.l	Channel14Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting

		CNOP	0,4

Channel15	dc.l	0		; instrument
		dc.l	Channel15Map	; note to voice map
		dc.l	0		; pitch wheel setting
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; volume
		dc.b	128		; pan setting


		CNOP	0,4

Channel0Map	dcb.b	128,255
Channel1Map	dcb.b	128,255
Channel2Map	dcb.b	128,255
Channel3Map	dcb.b	128,255
Channel4Map	dcb.b	128,255
Channel5Map	dcb.b	128,255
Channel6Map	dcb.b	128,255
Channel7Map	dcb.b	128,255
Channel8Map	dcb.b	128,255
Channel9Map	dcb.b	128,255
Channel10Map	dcb.b	128,255
Channel11Map	dcb.b	128,255
Channel12Map	dcb.b	128,255
Channel13Map	dcb.b	128,255
Channel14Map	dcb.b	128,255
Channel15Map	dcb.b	128,255

;--------------------------------------

		STRUCTURE AudioVoice,0
		APTR	vc_Next
		APTR	vc_Channel
		APTR	vc_Wave
		ULONG	vc_Index
		ULONG	vc_Step
		ULONG	vc_Loop
		ULONG	vc_Length
		ULONG	vc_LtVol
		ULONG	vc_RtVol
		BYTE	vc_Flags	; b7 = SFX, b1 = RLS, b0 = EN
		BYTE	vc_Num		; voice index to handle VoiceAvail
		BYTE	vc_Note		; note index into the channel map

		CNOP	0,4

Voices		dc.l	Voice0,Voice1,Voice2,Voice3
		dc.l	Voice4,Voice5,Voice6,Voice7
		dc.l	Voice8,Voice9,Voice10,Voice11
		dc.l	Voice12,Voice13,Voice14,Voice15
		dc.l	Voice16,Voice17,Voice18,Voice19
		dc.l	Voice20,Voice21,Voice22,Voice23
		dc.l	Voice24,Voice25,Voice26,Voice27
		dc.l	Voice28,Voice29,Voice30,Voice31

; Music Voices

		CNOP	0,4

Voice0		dc.l	Voice1
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	0		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice1		dc.l	Voice2
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	1		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice2		dc.l	Voice3
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	2		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice3		dc.l	Voice4
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	3		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice4		dc.l	Voice5
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	4		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice5		dc.l	Voice6
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	5		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice6		dc.l	Voice7
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	6		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice7		dc.l	Voice8
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	7		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice8		dc.l	Voice9
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	8		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice9		dc.l	Voice10
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	9		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice10		dc.l	Voice11
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	10		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice11		dc.l	Voice12
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	11		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice12		dc.l	Voice13
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	12		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice13		dc.l	Voice14
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	13		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice14		dc.l	Voice15
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	14		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice15		dc.l	Voice16
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	15		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice16		dc.l	Voice17
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	16		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice17		dc.l	Voice18
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	17		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice18		dc.l	Voice19
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	18		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice19		dc.l	Voice20
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	19		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice20		dc.l	Voice21
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	20		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice21		dc.l	Voice22
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	21		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice22		dc.l	Voice23
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	22		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice23		dc.l	Voice24
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	23		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice24		dc.l	Voice25
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	24		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice25		dc.l	Voice26
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	25		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice26		dc.l	Voice27
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	26		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice27		dc.l	Voice28
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	27		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice28		dc.l	Voice29
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	28		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice29		dc.l	Voice30
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	29		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice30		dc.l	Voice31
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	30		; voice index number
		dc.b	0		; note index

		CNOP	0,4

Voice31		dc.l	0
		dc.l	0		; channel back-link
		dc.l	0		; instrument wave data
		dc.l	0		; sample index
		dc.l	0		; sample rate
		dc.l	0		; instrument loop point
		dc.l	0		; instrument data length
		dc.l	0		; left volume table
		dc.l	0		; right volume table
		dc.b	0		; voice flags
		dc.b	31		; voice index number
		dc.b	0		; note index

;--------------------------------------

		STRUCTURE InstrumentRec,0
		APTR	in_Wave
		ULONG	in_Loop
		ULONG	in_Length
		BYTE	in_Flags
		BYTE	in_Base


		CNOP	0,4

Instruments	dcb.l	256,0

		CNOP	0,4

QuietInst	dc.l	0
		dc.l	0
		dc.l	0
		dc.b	0
		dc.b	0


		CNOP	0,4

InstrHandle	dc.l	0
InstrFile	dc.l	InstrName
InstrPool	dc.l	0

InstrName	dc.b	'MIDI_Instruments',0

		CNOP	0,4

validInstr	dc.b	%11111111	; (00-07) Piano
		dc.b	%11111111	; (08-0F) Chrom Perc
		dc.b	%11111111	; (10-17) Organ
		dc.b	%11111111	; (18-1F) Guitar
		dc.b	%11111111	; (20-27) Bass
		dc.b	%11111111	; (28-2F) Strings
		dc.b	%11111111	; (30-37) Ensemble
		dc.b	%11111111	; (38-3F) Brass
		dc.b	%11111111	; (40-47) Reed
		dc.b	%11111111	; (48-4F) Pipe
		dc.b	%11111111	; (50-57) Synth Lead
		dc.b	%11111111	; (58-5F) Synth Pad
		dc.b	%11111111	; (60-67) Synth Effects
		dc.b	%11111111	; (68-6F) Ethnic
		dc.b	%11111111	; (70-77) Percussive
		dc.b	%11111111	; (78-7F) SFX
		dc.b	%00000001	; (80-87) invalid,Drum
		dc.b	%11111111	; (88-8F) Drums/Clap/Hi-Hat
		dc.b	%11111111	; (90-97) Hi-Hats/Toms/Cymb1
		dc.b	%11111111	; (98-9F) Cymbals/Bells/Slap
		dc.b	%11111111	; (A0-A7) Bongos/Congas/Timb
		dc.b	%11111111	; (A8-AF) Agogo/Whistles/Gui
		dc.b	%11111100	; (B0-B7) Claves/Block/Trian
		dc.b	%00000000	; (B8-BF) invalid
		dc.b	%00000000	; (C0-C7)
		dc.b	%00000000	; (C8-CF)
		dc.b	%00000000	; (D0-D7)
		dc.b	%00000000	; (D8-DF)
		dc.b	%00000000	; (E0-E7)
		dc.b	%00000000	; (E8-EF)
		dc.b	%00000000	; (F0-F7)
		dc.b	%00000000	; (F8-FF)
;------------------------------------------------------------------------

;		section	PlayMusBSS,bss

;tempAudio	ds.b	256 ; TODO REMOVE

;------------------------------------------------------------------------

		end