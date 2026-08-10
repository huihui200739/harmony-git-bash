import { RepositoryCredentials } from './GitRepositoryService';

const URL_CREDENTIAL_PATTERN: RegExp =
  /([A-Za-z][A-Za-z0-9+.-]*:\/\/)[^/@\s'"]+@/g;
const BASIC_AUTH_PATTERN: RegExp =
  /(Authorization\s*:\s*Basic\s+)[A-Za-z0-9+/=_-]+/gi;
const BEARER_AUTH_PATTERN: RegExp =
  /(Authorization\s*:\s*Bearer\s+)[A-Za-z0-9._~+/=-]+/gi;
const SECRET_OPTION_PATTERN: RegExp =
  /((?:--)?(?:password|passwd|token|secret)(?:=|\s+))(?:"[^"]*"|'[^']*'|[^\s]+)/gi;

export function redactCredentialText(value: string): string {
  let redacted: string = value.replace(
    URL_CREDENTIAL_PATTERN,
    '$1'
  );
  redacted = redacted.replace(
    BASIC_AUTH_PATTERN,
    '$1<redacted>'
  );
  redacted = redacted.replace(
    BEARER_AUTH_PATTERN,
    '$1<redacted>'
  );
  return redacted.replace(
    SECRET_OPTION_PATTERN,
    '$1<redacted>'
  );
}

export function credentialTarget(value: string): string {
  return redactCredentialText(value.trim());
}

export function extractEmbeddedCredentials(
  value: string
): RepositoryCredentials | undefined {
  let searchFrom: number = 0;
  while (searchFrom < value.length) {
    const schemeSeparator: number = value.indexOf('://', searchFrom);
    if (schemeSeparator < 0) {
      return undefined;
    }
    const authorityStart: number = schemeSeparator + 3;
    let authorityEnd: number = value.length;
    for (let index: number = authorityStart; index < value.length; index++) {
      const character: string = value[index];
      if (character === '/' || character === '?' || character === '#' ||
          character === ' ' || character === '\t' || character === '\'' ||
          character === '"') {
        authorityEnd = index;
        break;
      }
    }
    const at: number = value.lastIndexOf('@', authorityEnd);
    if (at > authorityStart) {
      const userInfo: string = value.slice(authorityStart, at);
      const separator: number = userInfo.indexOf(':');
      if (separator >= 0) {
        return {
          username: decodeUrlPart(userInfo.slice(0, separator)),
          password: decodeUrlPart(userInfo.slice(separator + 1))
        };
      }
    }
    searchFrom = authorityEnd + 1;
  }
  return undefined;
}

export function credentialAlias(value: string): string {
  const target: string = credentialTarget(value);
  let first: number = 2166136261;
  let second: number = 2246822519;
  for (let index: number = 0; index < target.length; index++) {
    const code: number = target.charCodeAt(index);
    first = ((first ^ code) * 16777619) >>> 0;
    second = ((second ^ (code + index)) * 3266489917) >>> 0;
  }
  return `harmony-git-bash-v1-${hex32(first)}-${hex32(second)}`;
}

function decodeUrlPart(value: string): string {
  try {
    return decodeURIComponent(value);
  } catch (_error) {
    return value;
  }
}

function hex32(value: number): string {
  const text: string = value.toString(16);
  return `${'00000000'.slice(text.length)}${text}`;
}
