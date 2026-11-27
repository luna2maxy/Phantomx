---- MODULE bft_safety ----
EXTENDS Naturals, FiniteSets, Sequences

CONSTANTS Validators, Quorum, MaxView, MaxHeight, Hashes

VARIABLES view, height, lockedHash, lockedView, finalized, votes, viewChanges

Vars == <<view, height, lockedHash, lockedView, finalized, votes, viewChanges>>

TypeOK ==
  /\ view \in 0..MaxView
  /\ height \in 0..MaxHeight
  /\ lockedHash \in Hashes \union {"none"}
  /\ lockedView \in 0..MaxView
  /\ finalized \in SUBSET (0..MaxHeight \X Hashes)
  /\ votes \subseteq (Validators \X {"prevote","precommit"} \X 0..MaxView \X 0..MaxHeight \X Hashes)
  /\ viewChanges \subseteq (Validators \X 0..MaxView)

Init ==
  /\ view = 0 /\ height = 0
  /\ lockedHash = "none" /\ lockedView = 0
  /\ finalized = {}
  /\ votes = {} /\ viewChanges = {}

QuorumVotes(ph, v, h, hash) ==
  {val \in Validators : <<val, ph, v, h, hash>> \in votes}

HasQuorum(ph, v, h, hash) == Cardinality(QuorumVotes(ph, v, h, hash)) >= Quorum

Vote(val, ph, v, h, hash) ==
  /\ v = view
  /\ <<val, ph, v, h, hash>> \notin votes
  /\ votes' = votes \union {<<val, ph, v, h, hash>>}
  /\ UNCHANGED <<view,height,lockedHash,lockedView,finalized,viewChanges>>

Lock(h, hash, v) ==
  /\ HasQuorum("precommit", v, h, hash)
  /\ lockedHash' = hash /\ lockedView' = v
  /\ UNCHANGED <<view,height,finalized,votes,viewChanges>>

Finalize(h, hash, v) ==
  /\ HasQuorum("precommit", v, h, hash)
  /\ finalized' = finalized \union {<<h, hash>>}
  /\ height' = IF h > height THEN h ELSE height
  /\ UNCHANGED <<view,lockedHash,lockedView,votes,viewChanges>>

Timeout ==
  /\ view < MaxView
  /\ view' = view + 1
  /\ votes' = {}
  /\ UNCHANGED <<height,lockedHash,lockedView,finalized,viewChanges>>

NoConflictingFinality ==
  \A h \in 0..MaxHeight, ha, hb \in Hashes:
    <<h,ha>> \in finalized /\ <<h,hb>> \in finalized => ha = hb

LockSafety ==
  lockedHash # "none" => \A h \in 0..MaxHeight, hash \in Hashes:
    <<h,hash>> \in finalized /\ h >= 0 => TRUE

Next == \E val \in Validators, ph \in {"prevote","precommit"}, v \in 0..MaxView, h \in 0..MaxHeight, hash \in Hashes:
  Vote(val,ph,v,h,hash) \/ Lock(h,hash,v) \/ Finalize(h,hash,v) \/ Timeout

Spec == Init /\ [][Next]_Vars

THEOREM Spec => []TypeOK
THEOREM Spec => []NoConflictingFinality
====
