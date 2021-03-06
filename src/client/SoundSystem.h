#pragma once
#include "hge.h"

#include <map>

using namespace std;


class SoundSystem
{
public:
	SoundSystem(void);
	static SoundSystem& Instance();
	static void init(HGE* pHge);

	~SoundSystem(void);
	void PlaySound(int nID,int nPer = 100);
	void PlayMusic(int nID);
	void StopMusic(int nID);
	bool GetEnable() {return m_bEnableSound;};
	void SetEnable(bool bEnableSound);
private:
	map<int,HEFFECT> m_mapSound;
	map<int,HEFFECT> m_mapMusic;
	HCHANNEL m_pBg;
	bool m_bEnableSound;
	static HGE* m_pHge;
};
