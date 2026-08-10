import { describe, expect, it } from '@ohos/hypium';
import {
  credentialAlias,
  credentialTarget,
  extractEmbeddedCredentials,
  redactCredentialText
} from '../main/ets/git/CredentialSecurity';
import {
  MemoryCredentialStore
} from '../main/ets/git/CredentialStore';
import { RepositoryCredentials } from '../main/ets/git/GitRepositoryService';

export default function credentialSecurityTest(): void {
  describe('CredentialSecurity', (): void => {
    it('redacts URL, authorization and secret option values', 0,
      (): void => {
        const value: string = redactCredentialText(
          'git clone ' +
          'https://user:secret@example.invalid/repository.git ' +
          '--password "password with spaces" ' +
          'Authorization: Basic dXNlcjpzZWNyZXQ= ' +
          'Authorization: Bearer bearer-token'
        );
        expect(value).assertEqual(
          'git clone https://example.invalid/repository.git ' +
          '--password <redacted> ' +
          'Authorization: Basic <redacted> ' +
          'Authorization: Bearer <redacted>'
        );
      });

    it('extracts and decodes embedded URL credentials', 0, (): void => {
      const credentials: RepositoryCredentials | undefined =
        extractEmbeddedCredentials(
          'https://alice:pa%40ss@example.invalid/repository.git'
        );
      expect(credentials?.username).assertEqual('alice');
      expect(credentials?.password).assertEqual('pa@ss');
      expect(credentialTarget(
        'https://alice:pa%40ss@example.invalid/repository.git'
      )).assertEqual(
        'https://example.invalid/repository.git'
      );
    });

    it('creates a stable non-secret credential alias', 0, (): void => {
      const alias: string = credentialAlias(
        'https://example.invalid/repository.git'
      );
      expect(alias.startsWith('harmony-git-bash-v1-')).assertTrue();
      expect(alias.includes('repository.git')).assertFalse();
    });

    it('copies and removes credentials in memory', 0,
      async (): Promise<void> => {
        const store: MemoryCredentialStore =
          new MemoryCredentialStore();
        const original: RepositoryCredentials = {
          username: 'user',
          password: 'token'
        };
        await store.save('https://example.invalid/repository.git', original);
        original.password = 'changed';
        const loaded: RepositoryCredentials | undefined =
          await store.load(
            'https://example.invalid/repository.git'
          );
        expect(loaded?.password).assertEqual('token');
        if (loaded !== undefined) {
          loaded.password = 'mutated';
        }
        expect((await store.load(
          'https://example.invalid/repository.git'
        ))?.password).assertEqual('token');
        await store.remove('https://example.invalid/repository.git');
        expect(await store.load(
          'https://example.invalid/repository.git'
        )).assertEqual(undefined);
      });
  });
}
