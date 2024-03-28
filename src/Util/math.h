#pragma once

int Q_log2(int val)
{
	int answer = 0;
	while (val >>= 1)
		answer++;
	return answer;
}